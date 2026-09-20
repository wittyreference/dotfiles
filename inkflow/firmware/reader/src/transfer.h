// ABOUTME: WiFi transfer mode -- the device serves an upload page over its own access
// ABOUTME: point so books reach the SD card without ever removing it.

#pragma once

#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

#include "card_path.h"
#include "config.h"

/// Runs an access point and a small upload server while the reader is in transfer mode.
///
/// An access point rather than joining a home network: it needs no credentials, works
/// away from home, and cannot be broken by a router change. The cost is that the
/// uploading machine loses its own internet while connected, which for a few seconds of
/// file copy is a fair trade.
///
/// The radio is the largest single power draw on a 650 mAh cell, so this is started
/// explicitly and stopped the moment transfer mode is left. It is never on in the
/// background.
class Transfer {
public:
    void begin() {
        if (running_) {
            return;
        }
        // Constructed here, not as a member. A WebServer built at static-init time runs
        // its constructor before setup() and before the Arduino core is up, which hangs
        // the device before Serial.begin -- no output, no display, no clue.
        if (server_ == nullptr) {
            server_ = new WebServer(kHttpPort);
        }
        WiFi.mode(WIFI_AP);
        WiFi.softAP(kApSsid, kApPassword);
        ip_ = WiFi.softAPIP();

        server_->on("/", HTTP_GET, [this]() { serveIndex(); });
        server_->on("/list", HTTP_GET, [this]() { serveList(); });
        server_->on("/delete", HTTP_GET, [this]() { handleDelete(); });
        // Two handlers: the second fires repeatedly as the body streams in, the first
        // once at the end. A 1.4 MB book cannot be buffered whole, so it is written to
        // the card as it arrives.
        server_->on(
            "/upload", HTTP_POST, [this]() { finishUpload(); }, [this]() { streamUpload(); });
        server_->begin();
        running_ = true;
    }

    void end() {
        if (!running_) {
            return;
        }
        server_->stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        running_ = false;
    }

    void poll() {
        if (running_) {
            server_->handleClient();
        }
    }

    bool running() const { return running_; }
    IPAddress ip() const { return ip_; }
    uint8_t clients() const { return running_ ? WiFi.softAPgetStationNum() : 0; }
    const char* lastUpload() const { return lastName_; }
    size_t lastBytes() const { return lastBytes_; }
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    void serveIndex() {
        // Deliberately one self-contained page with no external assets: the client is
        // joined to an access point with no internet, so anything fetched from a CDN
        // would simply fail.
        server_->send(200, "text/html",
                     "<!doctype html><meta name=viewport content='width=device-width'>"
                     "<title>inkflow</title>"
                     "<style>body{font:16px system-ui;margin:2rem auto;max-width:34rem;"
                     "padding:0 1rem}h1{font-size:1.3rem}form{border:1px solid #ccc;"
                     "padding:1rem;border-radius:8px}input[type=file]{width:100%}"
                     "button{padding:.6rem 1.2rem;font-size:1rem;margin-top:.8rem}"
                     "#l{margin-top:1.5rem}</style>"
                     "<h1>inkflow</h1>"
                     "<form method=POST action=/upload enctype=multipart/form-data>"
                     "<input type=file name=f accept='.rsvp,.txt' required>"
                     "<button>Upload to reader</button></form>"
                     "<div id=l>loading files...</div>"
                     "<script>fetch('/list').then(r=>r.text()).then(t=>"
                     "document.getElementById('l').innerHTML=t)</script>");
    }

    void serveList() {
        String out = "<b>On the card</b><ul>";
        File root = SD.open("/");
        if (root) {
            for (File f = root.openNextFile(); f; f = root.openNextFile()) {
                if (!f.isDirectory()) {
                    out += "<li>";
                    out += f.name();
                    out += " &mdash; ";
                    out += String(f.size() / 1024);
                    out += " KB</li>";
                }
                f.close();
            }
            root.close();
        }
        out += "</ul>";
        server_->send(200, "text/html", out);
    }

    void handleDelete() {
        if (!server_->hasArg("f")) {
            server_->send(400, "text/plain", "missing f");
            return;
        }
        char path[kMaxCardPath];
        if (!safeCardPath(server_->arg("f").c_str(), path, sizeof(path))) {
            server_->send(400, "text/plain", "not a filename");
            return;
        }
        server_->send(200, "text/plain", SD.remove(path) ? "deleted" : "failed");
        dirty_ = true;
    }

    void streamUpload() {
        HTTPUpload& up = server_->upload();
        if (up.status == UPLOAD_FILE_START) {
            lastBytes_ = 0;
            lastFailure_ = nullptr;
            strncpy(lastName_, up.filename.c_str(), sizeof(lastName_) - 1);

            char path[kMaxCardPath];
            if (!safeCardPath(up.filename.c_str(), path, sizeof(path))) {
                lastFailure_ = "That is not a filename this device will write to the card.";
                return;
            }
            // Overwrite rather than append: re-uploading a corrected file should replace
            // it, not silently double it.
            SD.remove(path);
            file_ = SD.open(path, FILE_WRITE);
            if (!file_) {
                lastFailure_ = "The card would not open the file for writing.";
            }
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (file_) {
                const size_t wrote = file_.write(up.buf, up.currentSize);
                lastBytes_ += wrote;
                // A short write is the card filling up or failing. Without this the bytes
                // vanish and the page still congratulates the reader.
                if (wrote != up.currentSize && lastFailure_ == nullptr) {
                    lastFailure_ = "The card stopped accepting data part-way through.";
                }
            }
        } else if (up.status == UPLOAD_FILE_END) {
            if (file_) {
                file_.close();
            }
            dirty_ = true;
        }
    }

    /// Reports what actually happened, which is not always success.
    ///
    /// This used to render "Uploaded" unconditionally. If the card never opened, every
    /// write was silently dropped and the reader was told their book had arrived, at
    /// 0 KB. On a transfer path with no test coverage and one shot at a reader's trust,
    /// a page that lies about the outcome is worse than no page.
    void finishUpload() {
        String body = "<!doctype html><meta name=viewport content='width=device-width'>"
                      "<style>body{font:16px system-ui;margin:2rem auto;max-width:34rem;"
                      "padding:0 1rem}</style>";
        if (lastFailure_ != nullptr) {
            body += "<h1>Upload failed</h1><p>";
            body += lastName_;
            body += "</p><p>";
            body += lastFailure_;
            body += "</p><p><a href=/>Try again</a></p>";
            server_->send(500, "text/html", body);
            return;
        }
        body += "<h1>Uploaded</h1><p>";
        body += lastName_;
        body += " &mdash; ";
        body += String(lastBytes_ / 1024);
        body += " KB</p><p>Press <b>Right</b> on the reader to leave transfer mode and "
                "open it.</p><p><a href=/>Upload another</a></p>";
        server_->send(200, "text/html", body);
    }

    WebServer* server_ = nullptr;
    File file_;
    IPAddress ip_;
    bool running_ = false;
    bool dirty_ = false;
    char lastName_[64] = {0};
    size_t lastBytes_ = 0;
    /// Null when the last upload succeeded; otherwise what to tell the reader.
    const char* lastFailure_ = nullptr;
};
