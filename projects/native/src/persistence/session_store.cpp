#include "engcoder/persistence/session_store.hpp"
#include <sqlite3.h>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace engcoder::persistence {
namespace {
core::Error db_error(sqlite3* db, std::string prefix) { return {core::make_error_code(core::Errc::persistence_error), std::move(prefix)+": "+(db?sqlite3_errmsg(db):"sqlite")}; }
std::int64_t millis(std::chrono::system_clock::time_point tp){return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();}
}
SessionStore::SessionStore(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    if(sqlite3_open_v2(path.string().c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("failed to open session database");
    const char* schema=R"SQL(
PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS projects(id TEXT PRIMARY KEY, root TEXT NOT NULL, created_ms INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY, project_id TEXT NOT NULL, title TEXT, created_ms INTEGER NOT NULL, FOREIGN KEY(project_id) REFERENCES projects(id));
CREATE TABLE IF NOT EXISTS messages(id TEXT PRIMARY KEY, session_id TEXT NOT NULL, parent_id TEXT, role TEXT NOT NULL, content TEXT NOT NULL, tool_calls TEXT NOT NULL, tool_call_id TEXT NOT NULL, created_ms INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id, created_ms);
CREATE TABLE IF NOT EXISTS events(rowid_pk INTEGER PRIMARY KEY AUTOINCREMENT, sequence INTEGER NOT NULL, project_id TEXT, session_id TEXT, task_id TEXT, type TEXT NOT NULL, data TEXT NOT NULL, created_ms INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS idx_events_session_sequence ON events(session_id, sequence);
CREATE INDEX IF NOT EXISTS idx_events_session_task_type ON events(session_id, task_id, type);
)SQL";
    char* err=nullptr;if(sqlite3_exec(db_,schema,nullptr,nullptr,&err)!=SQLITE_OK){std::string msg=err?err:"schema error";sqlite3_free(err);throw std::runtime_error(msg);}
}
SessionStore::~SessionStore(){if(db_)sqlite3_close(db_);}
std::expected<void,core::Error> SessionStore::exec_locked(std::string_view sql){char*err=nullptr;if(sqlite3_exec(db_,std::string(sql).c_str(),nullptr,nullptr,&err)!=SQLITE_OK){std::string msg=err?err:"sqlite";sqlite3_free(err);return std::unexpected(core::Error{core::make_error_code(core::Errc::persistence_error),msg});}return{};}

std::expected<void,core::Error> SessionStore::ensure_project(std::string_view id,std::string_view root){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="INSERT OR IGNORE INTO projects(id,root,created_ms) VALUES(?,?,?)";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare project"));sqlite3_bind_text(s,1,id.data(),static_cast<int>(id.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,2,root.data(),static_cast<int>(root.size()),SQLITE_TRANSIENT);sqlite3_bind_int64(s,3,millis(std::chrono::system_clock::now()));const auto rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return std::unexpected(db_error(db_,"insert project"));return{};}
std::expected<void,core::Error> SessionStore::ensure_session(std::string_view id,std::string_view pid,std::string_view title){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="INSERT OR IGNORE INTO sessions(id,project_id,title,created_ms) VALUES(?,?,?,?)";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare session"));sqlite3_bind_text(s,1,id.data(),static_cast<int>(id.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,2,pid.data(),static_cast<int>(pid.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,3,title.data(),static_cast<int>(title.size()),SQLITE_TRANSIENT);sqlite3_bind_int64(s,4,millis(std::chrono::system_clock::now()));const auto rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return std::unexpected(db_error(db_,"insert session"));return{};}
std::expected<void,core::Error> SessionStore::append_message(std::string_view sid,std::string_view parent,std::string_view id,const model::Message&m){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="INSERT INTO messages(id,session_id,parent_id,role,content,tool_calls,tool_call_id,created_ms) VALUES(?,?,?,?,?,?,?,?)";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare message"));sqlite3_bind_text(s,1,id.data(),static_cast<int>(id.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,2,sid.data(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,3,parent.data(),static_cast<int>(parent.size()),SQLITE_TRANSIENT);sqlite3_bind_text(s,4,m.role.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,5,m.content.c_str(),-1,SQLITE_TRANSIENT);const auto tc=m.tool_calls.dump();sqlite3_bind_text(s,6,tc.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,7,m.tool_call_id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,8,millis(std::chrono::system_clock::now()));const auto rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return std::unexpected(db_error(db_,"insert message"));return{};}
std::expected<void,core::Error> SessionStore::append_event(const core::Event&e){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="INSERT INTO events(sequence,project_id,session_id,task_id,type,data,created_ms) VALUES(?,?,?,?,?,?,?)";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare event"));sqlite3_bind_int64(s,1,static_cast<sqlite3_int64>(e.sequence));sqlite3_bind_text(s,2,e.project_id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,3,e.session_id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,4,e.task_id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,5,e.type.c_str(),-1,SQLITE_TRANSIENT);const auto d=e.data.dump();sqlite3_bind_text(s,6,d.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,7,millis(e.timestamp));const auto rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return std::unexpected(db_error(db_,"insert event"));return{};}
std::expected<std::uint64_t,core::Error> SessionStore::max_event_sequence(){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="SELECT COALESCE(MAX(sequence),0) FROM events";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare max event sequence"));const auto rc=sqlite3_step(s);if(rc!=SQLITE_ROW){sqlite3_finalize(s);return std::unexpected(db_error(db_,"read max event sequence"));}const auto value=static_cast<std::uint64_t>(sqlite3_column_int64(s,0));sqlite3_finalize(s);return value;}
std::expected<std::vector<core::Event>,core::Error> SessionStore::events_after(std::string_view sid,std::uint64_t seq,std::size_t limit){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="SELECT sequence,project_id,session_id,task_id,type,data,created_ms FROM events WHERE session_id=? AND sequence>? ORDER BY sequence LIMIT ?";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare events"));sqlite3_bind_text(s,1,sid.data(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,static_cast<sqlite3_int64>(seq));sqlite3_bind_int64(s,3,static_cast<sqlite3_int64>(limit));std::vector<core::Event> out;while(sqlite3_step(s)==SQLITE_ROW){core::Event e;e.sequence=static_cast<std::uint64_t>(sqlite3_column_int64(s,0));e.project_id=reinterpret_cast<const char*>(sqlite3_column_text(s,1));e.session_id=reinterpret_cast<const char*>(sqlite3_column_text(s,2));e.task_id=reinterpret_cast<const char*>(sqlite3_column_text(s,3));e.type=reinterpret_cast<const char*>(sqlite3_column_text(s,4));e.data=json::parse(reinterpret_cast<const char*>(sqlite3_column_text(s,5)));e.timestamp=std::chrono::system_clock::time_point(std::chrono::milliseconds(sqlite3_column_int64(s,6)));out.push_back(std::move(e));}sqlite3_finalize(s);return out;}
std::expected<std::vector<model::Message>,core::Error> SessionStore::load_messages(std::string_view sid,std::size_t limit){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="SELECT role,content,tool_calls,tool_call_id FROM (SELECT rowid AS ord,role,content,tool_calls,tool_call_id FROM messages WHERE session_id=? ORDER BY rowid DESC LIMIT ?) ORDER BY ord";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare messages"));sqlite3_bind_text(s,1,sid.data(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,static_cast<sqlite3_int64>(limit));std::vector<model::Message> out;while(sqlite3_step(s)==SQLITE_ROW){model::Message m;m.role=reinterpret_cast<const char*>(sqlite3_column_text(s,0));m.content=reinterpret_cast<const char*>(sqlite3_column_text(s,1));m.tool_calls=json::parse(reinterpret_cast<const char*>(sqlite3_column_text(s,2)));m.tool_call_id=reinterpret_cast<const char*>(sqlite3_column_text(s,3));out.push_back(std::move(m));}sqlite3_finalize(s);return out;}
std::expected<std::optional<std::string>, core::Error> SessionStore::learnable_reply(
    std::string_view session_id, std::string_view task_id) {
    std::scoped_lock lock(mutex_);
    constexpr auto query = R"SQL(
SELECT data FROM events AS terminal
WHERE session_id=? AND task_id=? AND type='task.completed'
AND NOT EXISTS (
    SELECT 1 FROM events AS iteration
    WHERE iteration.session_id=terminal.session_id AND iteration.task_id=terminal.task_id
    AND iteration.type='iteration.started'
)
ORDER BY sequence DESC LIMIT 1
)SQL";
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db_, query, -1, &raw, nullptr) != SQLITE_OK) {
        return std::unexpected(db_error(db_, "prepare learning provenance"));
    }
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    if (sqlite3_bind_text(raw, 1, session_id.data(), static_cast<int>(session_id.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(raw, 2, task_id.data(), static_cast<int>(task_id.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        return std::unexpected(db_error(db_, "bind learning provenance"));
    }
    const auto result = sqlite3_step(raw);
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) return std::unexpected(db_error(db_, "read learning provenance"));
    const auto* bytes = reinterpret_cast<const char*>(sqlite3_column_text(raw, 0));
    if (!bytes) return std::unexpected(db_error(db_, "read learning result"));
    const auto data = json::parse(bytes, nullptr, false);
    if (!data.is_object() || !data.contains("result") || !data.at("result").is_string()) return std::nullopt;
    return data.at("result").get<std::string>();
}

std::expected<void,core::Error> SessionStore::delete_session(std::string_view sid){
    std::scoped_lock l(mutex_);
    if(auto begun=exec_locked("BEGIN IMMEDIATE");!begun)return begun;
    const auto rollback=[this](){(void)exec_locked("ROLLBACK");};
    const auto erase=[&](const char* sql,const char* label)->std::expected<void,core::Error>{
        sqlite3_stmt*s=nullptr;
        if(sqlite3_prepare_v2(db_,sql,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,std::string{"prepare "}+label));
        sqlite3_bind_text(s,1,sid.data(),static_cast<int>(sid.size()),SQLITE_TRANSIENT);
        const auto rc=sqlite3_step(s);
        sqlite3_finalize(s);
        if(rc!=SQLITE_DONE)return std::unexpected(db_error(db_,std::string{"delete "}+label));
        return{};
    };
    if(auto result=erase("DELETE FROM events WHERE session_id=?","session events");!result){rollback();return result;}
    if(auto result=erase("DELETE FROM messages WHERE session_id=?","session messages");!result){rollback();return result;}
    if(auto result=erase("DELETE FROM sessions WHERE id=?","session");!result){rollback();return result;}
    if(auto committed=exec_locked("COMMIT");!committed){rollback();return committed;}
    return{};
}
std::expected<std::vector<json>,core::Error> SessionStore::search_sessions(std::string_view query,std::size_t limit){std::scoped_lock l(mutex_);sqlite3_stmt*s=nullptr;const char*q="SELECT session_id,role,substr(content,1,1200),created_ms FROM messages WHERE content LIKE ? ORDER BY rowid DESC LIMIT ?";if(sqlite3_prepare_v2(db_,q,-1,&s,nullptr)!=SQLITE_OK)return std::unexpected(db_error(db_,"prepare search"));const std::string pattern="%"+std::string(query)+"%";sqlite3_bind_text(s,1,pattern.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,2,static_cast<sqlite3_int64>(limit));std::vector<json> out;while(sqlite3_step(s)==SQLITE_ROW)out.push_back({{"session_id",reinterpret_cast<const char*>(sqlite3_column_text(s,0))},{"role",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"content",reinterpret_cast<const char*>(sqlite3_column_text(s,2))},{"created_ms",sqlite3_column_int64(s,3)}});sqlite3_finalize(s);return out;}
} // namespace engcoder::persistence
