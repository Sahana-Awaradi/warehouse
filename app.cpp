// app.cpp
// Simple C++ REST server for Warehouse Inventory
// - Uses cpp-httplib (single-header) and nlohmann::json (single-header)
// - Stores data in db.json and serves static files from ./public
// Build with C++17

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <filesystem>

// include single-header libraries (place them in externals/ as explained)
#include "externals/httplib.h"   // https://github.com/yhirose/cpp-httplib
#include "externals/json.hpp"    // https://github.com/nlohmann/json

using json = nlohmann::json;
namespace fs = std::filesystem;

std::mutex db_mutex;
json db = json::object();
const std::string DB_PATH = "db.json";

void load_db() {
  std::lock_guard<std::mutex> lock(db_mutex);
  try {
    if (!fs::exists(DB_PATH)) {
      db = json::object();
      db["items"] = json::array();
      std::ofstream o(DB_PATH);
      o << db.dump(2);
      o.close();
      return;
    }
    std::ifstream i(DB_PATH);
    i >> db;
    i.close();
    if (!db.is_object() || !db.contains("items") || !db["items"].is_array()) {
      db = json::object();
      db["items"] = json::array();
    }
  } catch (const std::exception &e) {
    std::cerr << "Error loading DB: " << e.what() << std::endl;
    db = json::object();
    db["items"] = json::array();
  }
}

bool save_db() {
  std::lock_guard<std::mutex> lock(db_mutex);
  try {
    // write atomically to temp then rename
    std::string tmp = DB_PATH + ".tmp";
    std::ofstream o(tmp);
    o << db.dump(2);
    o.close();
    fs::rename(tmp, DB_PATH);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error saving DB: " << e.what() << std::endl;
    return false;
  }
}

std::string make_backend_id() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
  static std::atomic<uint64_t> counter{0};
  auto c = counter.fetch_add(1);
  return "b-" + std::to_string(ms) + "-" + std::to_string(c);
}

int main(int argc, char** argv) {
  load_db();

  const int PORT = 3000;
  httplib::Server svr;

  // Serve static files from ./public
  if (fs::exists("public")) {
    svr.set_mount_point("/", "./public");
    std::cout << "Serving static files from ./public\n";
  } else {
    std::cout << "Warning: ./public not found — static files won't be served\n";
  }

  // GET /api/items
  svr.Get("/api/items", [&](const httplib::Request&, httplib::Response& res) {
    load_db(); // reload from disk in case other process changed it
    json out;
    out["isOk"] = true;
    out["data"] = db["items"];
    res.set_content(out.dump(2), "application/json");
  });

  // POST /api/items
  svr.Post("/api/items", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = json::parse(req.body);
      if (!body.contains("item_id") || !body.contains("item_name")) {
        res.status = 400;
        json out; out["isOk"] = false; out["error"] = "missing fields";
        res.set_content(out.dump(), "application/json");
        return;
      }

      json newItem = body;
      if (!newItem.contains("__backendId")) newItem["__backendId"] = make_backend_id();
      if (!newItem.contains("timestamp")) newItem["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

      {
        std::lock_guard<std::mutex> lock(db_mutex);
        db["items"].push_back(newItem);
        if (!save_db()) {
          res.status = 500;
          json out; out["isOk"] = false; out["error"] = "failed to save db";
          res.set_content(out.dump(), "application/json");
          return;
        }
      }

      json out; out["isOk"] = true; out["data"] = newItem;
      res.set_content(out.dump(), "application/json");
    } catch (const std::exception &e) {
      res.status = 400;
      json out; out["isOk"] = false; out["error"] = std::string("invalid json: ") + e.what();
      res.set_content(out.dump(), "application/json");
    }
  });

  // PUT /api/items/:id
  svr.Put(R"(/api/items/(\S+))", [&](const httplib::Request& req, httplib::Response& res) {
    auto id = req.matches[1]; // regex capture
    try {
      auto body = json::parse(req.body);
      bool found = false;
      {
        std::lock_guard<std::mutex> lock(db_mutex);
        for (auto &it : db["items"]) {
          if (it.contains("__backendId") && it["__backendId"].get<std::string>() == id) {
            // merge update
            for (auto it2 = body.begin(); it2 != body.end(); ++it2) {
              it[it2.key()] = it2.value();
            }
            found = true;
            break;
          }
        }
        if (!found) {
          res.status = 404;
          json out; out["isOk"] = false; out["error"] = "not found";
          res.set_content(out.dump(), "application/json");
          return;
        }
        if (!save_db()) {
          res.status = 500;
          json out; out["isOk"] = false; out["error"] = "failed to save db";
          res.set_content(out.dump(), "application/json");
          return;
        }
      }

      json out; out["isOk"] = true; out["data"] = nullptr;
      res.set_content(out.dump(), "application/json");

    } catch (const std::exception &e) {
      res.status = 400;
      json out; out["isOk"] = false; out["error"] = std::string("invalid json: ") + e.what();
      res.set_content(out.dump(), "application/json");
    }
  });

  // DELETE /api/items/:id
  svr.Delete(R"(/api/items/(\S+))", [&](const httplib::Request& req, httplib::Response& res) {
    auto id = req.matches[1];
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(db_mutex);
      auto &arr = db["items"];
      json newarr = json::array();
      for (auto &it : arr) {
        if (it.contains("__backendId") && it["__backendId"].get<std::string>() == id) {
          removed = true;
          continue;
        }
        newarr.push_back(it);
      }
      arr = newarr;
      if (removed) {
        if (!save_db()) {
          res.status = 500;
          json out; out["isOk"] = false; out["error"] = "failed to save db";
          res.set_content(out.dump(), "application/json");
          return;
        }
      }
    }
    if (!removed) {
      res.status = 404;
      json out; out["isOk"] = false; out["error"] = "not found";
      res.set_content(out.dump(), "application/json");
      return;
    }
    json out; out["isOk"] = true;
    res.set_content(out.dump(), "application/json");
  });

  // fallback for anything else - attempt to serve index.html (if static not mounted)
  svr.set_error_handler([](const httplib::Request &req, httplib::Response &res) {
    // On 404, we might want to return index.html for SPA:
    if (res.status == 404 && fs::exists("public/index.html")) {
      std::ifstream ifs("public/index.html");
      std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
      res.set_content(content, "text/html");
      res.status = 200;
    }
  });

  std::cout << "Server listening on http://localhost:" << PORT << "\n";
  svr.listen("0.0.0.0", PORT);

  return 0;
}
