// SPDX-License-Identifier: GPL-3.0-or-later
#include "port_dpi_tools.hpp"
#include "port_safety.hpp"
#include "port_updater.hpp"
#include "common_utils.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(MetaInfo* arg1, SceAppInstallPkgInfo* pkg_info, PlayGoInfo* arg2);
}

namespace {

const char* kRoots[] = {
  "/data/etaHEN/pkgs",
  "/data/etaHEN/updates",
  "/data/etaHENbin",
  "/user/data/tmp",
};

bool starts_with(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool safe_path(const std::string& path) {
  if (path.find("..") != std::string::npos || path.empty()) return false;
  for (const char* root : kRoots) {
    if (path == root) return true;
    std::string prefix = root;
    prefix += '/';
    if (starts_with(path, prefix.c_str())) return true;
  }
  return false;
}

std::string dirname_of(const std::string& path) {
  size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? "/" : path.substr(0, pos);
}

std::string lower(std::string value) {
  for (char& c : value) c = char(tolower(uint8_t(c)));
  return value;
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (uint8_t(c) < 0x20) {
          char buffer[8];
          snprintf(buffer, sizeof(buffer), "\\u%04x", uint8_t(c));
          out += buffer;
        } else {
          out += c;
        }
    }
  }
  return out;
}

bool respond(MHD_Connection* conn, unsigned status, const std::string& body, const char* mime) {
  MHD_Response* response = MHD_create_response_from_buffer(body.size(), const_cast<char*>(body.data()), MHD_RESPMEM_MUST_COPY);
  if (!response) return false;
  MHD_add_response_header(response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
  if (mime) MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mime);
  MHD_add_response_header(response, "Cache-Control", "no-store");
  bool ok = MHD_queue_response(conn, status, response) == MHD_YES;
  MHD_destroy_response(response);
  return ok;
}

std::string query(MHD_Connection* conn, const char* key) {
  const char* value = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, key);
  return value ? value : "";
}

std::string api_status() {
  std::string body = "{\"ok\":true,\"version\":\"";
  body += json_escape(ETAHEN_LOCAL_VERSION);
  body += "\",\"safe_mode\":";
  body += port_safety::safe_mode() ? "true" : "false";
  body += ",\"kernel_mode\":";
  body += port_safety::kernel_mode_active() ? "true" : "false";
  body += ",\"update_pending\":";
  body += port_safety::exists(port_safety::kUpdatePending) ? "true" : "false";
  body += "}";
  return body;
}

std::string api_files(const std::string& dir) {
  if (!safe_path(dir)) return "{\"ok\":false,\"error\":\"directory is not allowed\"}";

  std::string body = "{\"ok\":true,\"dir\":\"";
  body += json_escape(dir);
  body += "\",\"files\":[";

  DIR* handle = opendir(dir.c_str());
  if (handle) {
    struct Entry {
      std::string name;
      std::string path;
      bool directory;
      long long size;
      long long mtime;
    };
    std::vector<Entry> entries;
    struct dirent* entry;
    while ((entry = readdir(handle)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      std::string path = dir;
      path += '/';
      path += entry->d_name;
      struct stat st{};
      if (stat(path.c_str(), &st) != 0) continue;
      entries.push_back({entry->d_name, path, S_ISDIR(st.st_mode), (long long)st.st_size, (long long)st.st_mtime});
    }
    closedir(handle);

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      if (a.directory != b.directory) return a.directory > b.directory;
      return a.name < b.name;
    });

    for (size_t i = 0; i < entries.size(); i++) {
      if (i) body += ',';
      const Entry& e = entries[i];
      body += "{\"name\":\"";
      body += json_escape(e.name);
      body += "\",\"path\":\"";
      body += json_escape(e.path);
      body += "\",\"dir\":";
      body += e.directory ? "true" : "false";
      body += ",\"size\":";
      body += std::to_string(e.size);
      body += ",\"mtime\":";
      body += std::to_string(e.mtime);
      body += "}";
    }
  }

  body += "]}";
  return body;
}

bool api_delete(const std::string& path, std::string& error) {
  if (!safe_path(path)) { error = "path is not allowed"; return false; }
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) { error = "file not found"; return false; }
  if (S_ISDIR(st.st_mode)) { error = "directories cannot be deleted here"; return false; }
  if (unlink(path.c_str()) != 0) { error = "delete failed"; return false; }
  port_safety::append_log(port_safety::kCrashLog, "dpi", ("deleted " + path).c_str());
  return true;
}

bool api_rename(const std::string& path, const std::string& name, std::string& error) {
  if (!safe_path(path)) { error = "path is not allowed"; return false; }
  if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
      name.find("..") != std::string::npos) {
    error = "invalid name";
    return false;
  }

  std::string dest = dirname_of(path) + "/" + name;
  if (!safe_path(dest)) { error = "destination is not allowed"; return false; }
  if (rename(path.c_str(), dest.c_str()) != 0) { error = "rename failed"; return false; }
  return true;
}

bool api_install(const std::string& path, std::string& error) {
  if (!safe_path(path)) { error = "path is not allowed"; return false; }
  if (lower(path).find(".pkg") == std::string::npos) { error = "not a PKG file"; return false; }
  struct stat st{};
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) { error = "file not found"; return false; }

  sceAppInstUtilInitialize();

  PlayGoInfo playgo{};
  SceAppInstallPkgInfo pkg{};
  MetaInfo meta{};
  meta.uri = path.c_str();
  meta.ex_uri = "";
  meta.playgo_scenario_id = "";
  meta.content_id = "";
  meta.content_name = "etaHEN file manager";
  meta.icon_url = "";

  int result = sceAppInstUtilInstallByPackage(&meta, &pkg, &playgo);
  if (result != 0) {
    error = "install failed with code " + std::to_string(result);
    return false;
  }

  port_safety::append_log(port_safety::kCrashLog, "dpi", ("installing " + path).c_str());
  return true;
}

std::string tools_page() {
  return R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>etaHEN Tools</title>
<style>
body{font-family:Arial,sans-serif;background:#111318;color:#e8e8e8;margin:0;padding:24px}
h1,h2{font-weight:600} .card{background:#1b1f27;border:1px solid #2c3340;border-radius:8px;padding:18px;margin:16px 0}
button{background:#4285f4;color:#fff;border:0;border-radius:5px;padding:8px 12px;cursor:pointer;margin:2px}
button.warn{background:#c0392b} button.alt{background:#586474}
input,select{background:#0f1217;color:#e8e8e8;border:1px solid #39414f;border-radius:4px;padding:7px}
table{width:100%;border-collapse:collapse;margin-top:10px} td,th{padding:6px;border-bottom:1px solid #2c3340;text-align:left;font-size:14px}
pre{white-space:pre-wrap;background:#0f1217;border:1px solid #2c3340;padding:10px;border-radius:5px;max-height:180px;overflow:auto}
a{color:#7db4ff}.muted{color:#9aa4b2}.ok{color:#57d68d}.bad{color:#ff7676}
</style></head><body>
<h1>etaHEN Tools</h1>
<div class="card"><h2>Status</h2><div id="status" class="muted">loading...</div></div>
<div class="card"><h2>etaHEN updater <span class="muted">(Pinecone-22/etaHEN-)</span></h2>
<button onclick="checkUpdate()">Check for updates</button><button onclick="installUpdate()">Download and install</button>
<div id="update" class="muted"></div><pre id="notes"></pre></div>
<div class="card"><h2>PKG file manager</h2>
<div><select id="dir" onchange="loadFiles()">
<option>/data/etaHEN/pkgs</option><option>/user/data/tmp</option><option>/data/etaHEN/updates</option><option>/data/etaHENbin</option>
</select> <button onclick="loadFiles()">Refresh</button></div>
<table><thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead><tbody id="files"></tbody></table>
<div id="filemsg" class="muted"></div></div>
<div class="card"><h2>Recovery</h2><div class="muted">Safe mode is written to /data/etaHEN/safe_mode. A crash-loop boots without plugins and without Toolbox auto-start.</div></div>
<p><a href="/">Back to installer</a></p>
<script>
const fmt=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB';
async function get(url){const r=await fetch(url);return await r.json();}
async function status(){const s=await get('/api/status');document.getElementById('status').textContent=
 'Version '+s.version+' | safe mode '+(s.safe_mode?'ON':'off')+' | kernel mode '+(s.kernel_mode?'ON':'off')+' | update pending '+(s.update_pending?'yes':'no');}
async function checkUpdate(){const u=await get('/api/update/check');const el=document.getElementById('update');
 if(!u.ok){el.innerHTML='<span class="bad">'+u.error+'</span>';return;}
 el.innerHTML='Local '+u.local+' | remote '+u.version+' | '+(u.available?'<span class="ok">update available</span>':'up to date');
 document.getElementById('notes').textContent=u.notes||'';}
async function installUpdate(){const el=document.getElementById('update');el.textContent='Downloading and verifying...';
 const r=await fetch('/api/update/install',{method:'POST'});const u=await r.json();
 el.innerHTML=u.ok?'<span class="ok">Installed. Relaunch etaHEN or reboot to use '+u.version+'.</span>':'<span class="bad">'+u.error+'</span>';}
async function loadFiles(){const dir=document.getElementById('dir').value;const r=await get('/api/files?dir='+encodeURIComponent(dir));
 const body=document.getElementById('files');body.innerHTML='';document.getElementById('filemsg').textContent='';
 if(!r.ok){document.getElementById('filemsg').textContent=r.error||'failed';return;}
 function mk(text,cls,fn){const b=document.createElement('button');b.textContent=text;if(cls)b.className=cls;b.onclick=fn;return b;}
 for(const f of r.files){const tr=document.createElement('tr');const td1=document.createElement('td');
  td1.textContent=(f.dir?'[@] ':'')+f.name;tr.appendChild(td1);
  const td2=document.createElement('td');td2.textContent=f.dir?'-':fmt(f.size);tr.appendChild(td2);
  const td3=document.createElement('td');
  if(!f.dir){
   if(f.name.toLowerCase().endsWith('.pkg'))td3.appendChild(mk('Install','',()=>install(f.path)));
   const dl=document.createElement('a');dl.href='/'+f.path;dl.target='_blank';
   const dlb=document.createElement('button');dlb.textContent='Download';dlb.className='alt';dl.appendChild(dlb);td3.appendChild(dl);
   td3.appendChild(mk('Rename','',()=>rename(f.path)));
   td3.appendChild(mk('Delete','warn',()=>del(f.path)));
  }
  tr.appendChild(td3);body.appendChild(tr);}}
async function action(path,url){const r=await fetch(url,{method:'POST'});const j=await r.json();
 document.getElementById('filemsg').textContent=j.ok?'done':(j.error||'failed');if(j.ok)loadFiles();}
function install(p){action(p,'/api/files/install?path='+encodeURIComponent(p));}
function del(p){if(confirm('Delete '+p+'?'))action(p,'/api/files/delete?path='+encodeURIComponent(p));}
function rename(p){const n=prompt('New name',p.split('/').pop());if(n)action(p,'/api/files/rename?path='+encodeURIComponent(p)+'&name='+encodeURIComponent(n));}
status();loadFiles();
</script></body></html>)HTML";
}

}

bool port_dpi_tools_handle(MHD_Connection* conn, const char* url, const char* method) {
  port_safety::mkdir_p("/data/etaHEN/pkgs");
  port_safety::mkdir_p("/data/etaHEN/updates");
  port_safety::mkdir_p("/data/etaHENbin");

  const bool get = strcmp(method, MHD_HTTP_METHOD_GET) == 0;
  const bool post = strcmp(method, MHD_HTTP_METHOD_POST) == 0;

  if (get && strcmp(url, "/tools") == 0) {
    respond(conn, MHD_HTTP_OK, tools_page(), "text/html; charset=utf-8");
    return true;
  }
  if (get && strcmp(url, "/api/status") == 0) {
    respond(conn, MHD_HTTP_OK, api_status(), "application/json");
    return true;
  }
  if (get && strcmp(url, "/api/files") == 0) {
    std::string dir = query(conn, "dir");
    if (dir.empty()) dir = "/data/etaHEN/pkgs";
    respond(conn, MHD_HTTP_OK, api_files(dir), "application/json");
    return true;
  }
  if (get && strcmp(url, "/api/update/check") == 0) {
    PortUpdateInfo info;
    std::string error;
    bool ok = port_update_check(info, error);
    std::string body = "{\"ok\":";
    body += ok ? "true" : "false";
    body += ",\"local\":\"";
    body += json_escape(info.local_version);
    body += "\",\"version\":\"";
    body += json_escape(info.version);
    body += "\",\"available\":";
    body += info.available ? "true" : "false";
    body += ",\"size\":";
    body += std::to_string(info.size);
    body += ",\"sha256\":\"";
    body += json_escape(info.sha256);
    body += "\",\"notes\":\"";
    body += json_escape(info.notes.substr(0, 6000));
    body += "\",\"error\":\"";
    body += json_escape(error);
    body += "\"}";
    respond(conn, MHD_HTTP_OK, body, "application/json");
    return true;
  }
  if (post && strcmp(url, "/api/update/install") == 0) {
    PortUpdateInfo info;
    std::string error;
    bool ok = port_update_check(info, error);
    if (ok) ok = port_update_install(info, error);
    std::string body = "{\"ok\":";
    body += ok ? "true" : "false";
    body += ",\"version\":\"";
    body += json_escape(info.version);
    body += "\",\"error\":\"";
    body += json_escape(error);
    body += "\"}";
    respond(conn, MHD_HTTP_OK, body, "application/json");
    return true;
  }
  if (post && strcmp(url, "/api/files/delete") == 0) {
    std::string error;
    bool ok = api_delete(query(conn, "path"), error);
    respond(conn, MHD_HTTP_OK, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":\"" + json_escape(error) + "\"}", "application/json");
    return true;
  }
  if (post && strcmp(url, "/api/files/rename") == 0) {
    std::string error;
    bool ok = api_rename(query(conn, "path"), query(conn, "name"), error);
    respond(conn, MHD_HTTP_OK, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":\"" + json_escape(error) + "\"}", "application/json");
    return true;
  }
  if (post && strcmp(url, "/api/files/install") == 0) {
    std::string error;
    bool ok = api_install(query(conn, "path"), error);
    respond(conn, MHD_HTTP_OK, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":\"" + json_escape(error) + "\"}", "application/json");
    return true;
  }

  return false;
}
