/** @file
 * @brief Commit bounded room history with SQLite transactions and hashed resume capabilities.
 */
#include "chat/server/history_store.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <limits>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sqlite3.h>
#include <stdexcept>
#include <string_view>

namespace chat::server {
namespace {
void check(int code) {
    if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW) {
        throw std::runtime_error("history storage unavailable");
    }
}
void execute(sqlite3* db, const char* sql) {
    check(sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}
/// Prepared statement owns its SQLite handle and binds values without SQL concatenation.
class Statement {
  public:
    Statement(sqlite3* db, const char* sql) {
        check(sqlite3_prepare_v2(db, sql, -1, &value_, nullptr));
    }
    ~Statement() { static_cast<void>(sqlite3_finalize(value_)); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int index, std::int64_t value) { check(sqlite3_bind_int64(value_, index, value)); }
    void bind(int index, const std::string& value) {
        check(sqlite3_bind_text(value_, index, value.data(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT));
    }
    bool row() {
        const auto code = sqlite3_step(value_);
        check(code);
        return code == SQLITE_ROW;
    }
    void done() {
        if (row()) {
            throw std::runtime_error("unexpected storage result");
        }
    }
    std::int64_t number(int column) const { return sqlite3_column_int64(value_, column); }
    std::string text(int column) const {
        const auto* data = sqlite3_column_text(value_, column);
        const auto size = sqlite3_column_bytes(value_, column);
        return data ? std::string(reinterpret_cast<const char*>(data),
                                  static_cast<std::size_t>(size))
                    : std::string{};
    }

  private:
    sqlite3_stmt* value_ = nullptr;
};
/// Failed commits roll back; no uncommitted room/message can escape as an ACK.
class Transaction {
  public:
    explicit Transaction(sqlite3* db) : db_(db) { execute(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!committed_) {
            static_cast<void>(sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr));
        }
    }
    void commit() {
        execute(db_, "COMMIT");
        committed_ = true;
    }

  private:
    sqlite3* db_;
    bool committed_ = false;
};
std::string hex(const unsigned char* data, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result += digits[data[i] >> 4U];
        result += digits[data[i] & 15U];
    }
    return result;
}
std::string token_hash(const std::string& token) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
    unsigned int size = 0;
    if (EVP_Digest(token.data(), token.size(), output.data(), &size, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("identity generation unavailable");
    }
    return hex(output.data(), size);
}
std::string new_token() {
    std::array<unsigned char, 32> output{};
    if (RAND_bytes(output.data(), static_cast<int>(output.size())) != 1) {
        throw std::runtime_error("identity generation unavailable");
    }
    return hex(output.data(), output.size());
}
bool valid_token(const std::string& token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::int64_t room_number(const std::string& id) {
    const auto prefix = id.starts_with("direct-") ? 7U : id.starts_with("group-") ? 6U : 0U;
    std::int64_t number = 0;
    if (prefix == 0 || id.size() <= prefix) {
        return 0;
    }
    const auto value = std::string_view(id).substr(prefix);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number <= 0 ||
        std::to_string(number) != value) {
        return 0;
    }
    return number;
}
} // namespace

class HistoryStore::Impl {
  public:
    explicit Impl(const std::string& path) {
        try {
            check(sqlite3_open_v2(path.c_str(), &db,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                  nullptr));
            check(sqlite3_busy_timeout(db, 100));
            execute(db, "PRAGMA foreign_keys=ON");
            Statement journal(db, "PRAGMA journal_mode=WAL");
            if (!journal.row() || (path != ":memory:" && journal.text(0) != "wal")) {
                throw std::runtime_error("WAL storage unavailable");
            }
            journal.done();
            execute(db, "PRAGMA synchronous=FULL");
            Statement version(db, "PRAGMA user_version");
            if (!version.row() || version.number(0) < 0 || version.number(0) > 2) {
                throw std::runtime_error("unsupported history schema");
            }
            const auto schema = version.number(0);
            const bool fresh = schema == 0;
            version.done();
            if (fresh) {
                Transaction transaction(db);
                execute(
                    db,
                    "CREATE TABLE users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                    "token_hash TEXT NOT NULL UNIQUE);"
                    "CREATE TABLE rooms(id INTEGER PRIMARY KEY AUTOINCREMENT, kind INTEGER NOT "
                    "NULL CHECK(kind IN(0,1)), name TEXT NOT NULL DEFAULT '',"
                    "a INTEGER REFERENCES users(id), b INTEGER REFERENCES users(id), UNIQUE(a,b), "
                    "CHECK((kind=0 AND a IS NOT NULL AND b IS NOT NULL AND a<=b) OR (kind=1 AND a "
                    "IS NULL AND b IS NULL)));"
                    "CREATE TABLE admissions(room INTEGER NOT NULL REFERENCES rooms(id), principal "
                    "INTEGER NOT NULL REFERENCES users(id), PRIMARY KEY(room,principal));"
                    "CREATE TABLE messages(id INTEGER PRIMARY KEY AUTOINCREMENT, room INTEGER NOT "
                    "NULL REFERENCES rooms(id), principal INTEGER NOT NULL REFERENCES users(id),"
                    "sender TEXT NOT NULL, recipient TEXT NOT NULL, text TEXT NOT NULL);"
                    "CREATE INDEX messages_room_cursor ON messages(room,id);"
                    "PRAGMA user_version=1;");
                transaction.commit();
            }
            if (schema < 2) {
                Transaction transaction(db);
                execute(
                    db,
                    "ALTER TABLE rooms ADD COLUMN last_sequence INTEGER NOT NULL DEFAULT 0;"
                    "ALTER TABLE messages ADD COLUMN sequence INTEGER NOT NULL DEFAULT 0;"
                    "ALTER TABLE messages ADD COLUMN timestamp INTEGER NOT NULL DEFAULT 0;"
                    "ALTER TABLE messages ADD COLUMN client_key TEXT;"
                    "WITH ranks AS (SELECT id, row_number() OVER (PARTITION BY room ORDER BY id) "
                    "AS seq FROM messages) UPDATE messages SET sequence=(SELECT seq FROM ranks "
                    "WHERE ranks.id=messages.id);"
                    "UPDATE rooms SET last_sequence=(SELECT coalesce(max(sequence),0) FROM "
                    "messages WHERE messages.room=rooms.id);"
                    "CREATE UNIQUE INDEX messages_room_sequence ON messages(room,sequence);"
                    "CREATE UNIQUE INDEX messages_retry_key ON messages(principal,client_key);"
                    "PRAGMA user_version=2;");
                transaction.commit();
            }
        } catch (...) {
            static_cast<void>(sqlite3_close_v2(db));
            db = nullptr;
            throw;
        }
    }
    ~Impl() { static_cast<void>(sqlite3_close_v2(db)); }
    StoredMessage append(std::int64_t room, std::int64_t principal, const std::string& sender,
                         const std::string& recipient, const std::string& text,
                         const std::string& key, const std::string& room_id) {
        std::int64_t sequence = 0;
        {
            Statement query(db, "SELECT last_sequence FROM rooms WHERE id=?");
            query.bind(1, room);
            if (!query.row() || query.number(0) < 0 ||
                query.number(0) == std::numeric_limits<std::int64_t>::max()) {
                throw std::runtime_error("room sequence unavailable");
            }
            sequence = query.number(0) + 1;
        }
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
        Statement insert(
            db, "INSERT INTO "
                "messages(room,principal,sender,recipient,text,sequence,timestamp,client_key) "
                "VALUES(?,?,?,?,?,?,?,nullif(?,''))");
        insert.bind(1, room);
        insert.bind(2, principal);
        insert.bind(3, sender);
        insert.bind(4, recipient);
        insert.bind(5, text);
        insert.bind(6, sequence);
        insert.bind(7, timestamp);
        insert.bind(8, key);
        insert.done();
        const auto id = sqlite3_last_insert_rowid(db);
        Statement update(db, "UPDATE rooms SET last_sequence=? WHERE id=?");
        update.bind(1, sequence);
        update.bind(2, room);
        update.done();
        return {room_id, "message-" + std::to_string(id), static_cast<std::uint64_t>(sequence),
                static_cast<std::uint64_t>(timestamp), false};
    }
    sqlite3* db = nullptr;
};
HistoryStore::HistoryStore(const std::string& path) : impl_(std::make_unique<Impl>(path)) {}
HistoryStore::~HistoryStore() = default;
StoredIdentity HistoryStore::identity(const std::string& name, const std::string& token) {
    if (!token.empty()) {
        if (!valid_token(token)) {
            throw std::invalid_argument("invalid resume token");
        }
        Statement find(impl_->db, "SELECT id FROM users WHERE name=? AND token_hash=?");
        find.bind(1, name);
        find.bind(2, token_hash(token));
        if (!find.row()) {
            throw std::invalid_argument("invalid resume token");
        }
        return {find.number(0), token};
    }
    const auto issued = new_token();
    Statement insert(impl_->db, "INSERT INTO users(name,token_hash) VALUES(?,?)");
    insert.bind(1, name);
    insert.bind(2, token_hash(issued));
    insert.done();
    return {sqlite3_last_insert_rowid(impl_->db), issued};
}
std::vector<StoredGroup> HistoryStore::groups() {
    Statement query(impl_->db, "SELECT id,name FROM rooms WHERE kind=1 ORDER BY id");
    std::vector<StoredGroup> result;
    while (query.row()) {
        if (result.size() == 128) {
            throw std::runtime_error("stored group limit exceeded");
        }
        result.push_back({"group-" + std::to_string(query.number(0)), query.text(1)});
    }
    return result;
}
std::string HistoryStore::create_group(const std::string& name, std::int64_t principal) {
    Transaction transaction(impl_->db);
    Statement insert(impl_->db, "INSERT INTO rooms(kind,name) VALUES(1,?)");
    insert.bind(1, name);
    insert.done();
    const auto id = "group-" + std::to_string(sqlite3_last_insert_rowid(impl_->db));
    join_group(id, principal);
    transaction.commit();
    return id;
}
void HistoryStore::join_group(const std::string& id, std::int64_t principal) {
    Statement insert(impl_->db, "INSERT OR IGNORE INTO admissions(room,principal) VALUES(?,?)");
    insert.bind(1, room_number(id));
    insert.bind(2, principal);
    insert.done();
}
StoredMessage HistoryStore::direct(std::int64_t sender, std::int64_t recipient,
                                   const std::string& sender_name,
                                   const std::string& recipient_name, const std::string& text,
                                   const std::string& key) {
    Transaction transaction(impl_->db);
    if (auto prior = retry(sender, key, false, recipient_name, text)) {
        transaction.commit();
        return *prior;
    }
    const auto first = std::min(sender, recipient), second = std::max(sender, recipient);
    std::int64_t id = 0;
    {
        Statement query(impl_->db, "SELECT id FROM rooms WHERE kind=0 AND a=? AND b=?");
        query.bind(1, first);
        query.bind(2, second);
        if (query.row()) {
            id = query.number(0);
        }
    }
    if (id == 0) {
        Statement insert(impl_->db, "INSERT INTO rooms(kind,a,b) VALUES(0,?,?)");
        insert.bind(1, first);
        insert.bind(2, second);
        insert.done();
        id = sqlite3_last_insert_rowid(impl_->db);
    }
    auto result = impl_->append(id, sender, sender_name, recipient_name, text, key,
                                "direct-" + std::to_string(id));
    transaction.commit();
    return result;
}
StoredMessage HistoryStore::group_message(const std::string& id, std::int64_t sender,
                                          const std::string& sender_name, const std::string& text,
                                          const std::string& key) {
    Transaction transaction(impl_->db);
    if (auto prior = retry(sender, key, true, id, text)) {
        transaction.commit();
        return *prior;
    }
    auto result = impl_->append(room_number(id), sender, sender_name, {}, text, key, id);
    transaction.commit();
    return result;
}
std::optional<StoredMessage> HistoryStore::retry(std::int64_t sender, const std::string& key,
                                                 bool group, const std::string& target,
                                                 const std::string& text) {
    if (key.empty()) {
        return std::nullopt;
    }
    Statement query(
        impl_->db,
        "SELECT m.id,m.room,r.kind,m.recipient,m.text,m.sequence,m.timestamp "
        "FROM messages m JOIN rooms r ON r.id=m.room WHERE m.principal=? AND m.client_key=?");
    query.bind(1, sender);
    query.bind(2, key);
    if (!query.row()) {
        return std::nullopt;
    }
    const auto room =
        std::string(query.number(2) ? "group-" : "direct-") + std::to_string(query.number(1));
    if ((query.number(2) != 0) != group || (group ? room : query.text(3)) != target ||
        query.text(4) != text) {
        throw MessageIdConflict();
    }
    return StoredMessage{room, "message-" + std::to_string(query.number(0)),
                         static_cast<std::uint64_t>(query.number(5)),
                         static_cast<std::uint64_t>(query.number(6)), true};
}
bool HistoryStore::direct_participant(const std::string& id, std::int64_t principal) {
    if (!id.starts_with("direct-")) {
        return false;
    }
    Statement query(impl_->db, "SELECT 1 FROM rooms WHERE id=? AND kind=0 AND (a=? OR b=?)");
    query.bind(1, room_number(id));
    query.bind(2, principal);
    query.bind(3, principal);
    return query.row();
}
protocol::HistoryResponse HistoryStore::history(const std::string& id, std::uint64_t after,
                                                std::uint32_t limit) {
    if (limit == 0 || limit > 100 ||
        after > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("invalid history page bounds");
    }
    protocol::HistoryResponse result;
    result.set_room_id(id);
    result.set_group(id.starts_with("group-"));
    result.set_next_after_id(after);
    Statement query(impl_->db, "SELECT id,sender,recipient,text,sequence,timestamp FROM messages "
                               "WHERE room=? AND id>? ORDER BY id LIMIT ?");
    query.bind(1, room_number(id));
    query.bind(2, static_cast<std::int64_t>(after));
    query.bind(3, static_cast<std::int64_t>(limit) + 1);
    while (query.row()) {
        if (static_cast<std::uint32_t>(result.entries_size()) == limit) {
            result.set_has_more(true);
            break;
        }
        auto* entry = result.add_entries();
        entry->set_id(static_cast<std::uint64_t>(query.number(0)));
        entry->set_sender(query.text(1));
        entry->set_recipient(query.text(2));
        entry->set_text(query.text(3));
        entry->set_message_id("message-" + std::to_string(query.number(0)));
        entry->set_room_sequence(static_cast<std::uint64_t>(query.number(4)));
        entry->set_server_timestamp_unix_ms(static_cast<std::uint64_t>(query.number(5)));
        result.set_next_after_id(entry->id());
    }
    return result;
}
protocol::SyncResponse HistoryStore::sync(const std::string& id, std::uint64_t after,
                                          std::uint64_t through, std::uint32_t limit) {
    protocol::SyncResponse result;
    result.set_room_id(id);
    result.set_group(id.starts_with("group-"));
    std::uint64_t latest = 0;
    {
        Statement query(impl_->db, "SELECT last_sequence FROM rooms WHERE id=?");
        query.bind(1, room_number(id));
        if (!query.row() || query.number(0) < 0) {
            throw std::invalid_argument("sync gap");
        }
        latest = static_cast<std::uint64_t>(query.number(0));
    }
    if (through == 0) {
        through = latest;
    }
    if (after > through || through > latest || limit == 0 || limit > 100) {
        throw std::invalid_argument("sync gap");
    }
    result.set_through_sequence(through);
    result.set_next_sequence(after);
    Statement query(impl_->db,
                    "SELECT id,sender,recipient,text,sequence,timestamp FROM messages "
                    "WHERE room=? AND sequence>? AND sequence<=? ORDER BY sequence LIMIT ?");
    query.bind(1, room_number(id));
    query.bind(2, static_cast<std::int64_t>(after));
    query.bind(3, static_cast<std::int64_t>(through));
    query.bind(4, static_cast<std::int64_t>(limit));
    while (query.row()) {
        const auto sequence = static_cast<std::uint64_t>(query.number(4));
        if (sequence != result.next_sequence() + 1) {
            throw std::invalid_argument("sync gap");
        }
        auto* entry = result.add_entries();
        entry->set_id(static_cast<std::uint64_t>(query.number(0)));
        entry->set_sender(query.text(1));
        entry->set_recipient(query.text(2));
        entry->set_text(query.text(3));
        entry->set_message_id("message-" + std::to_string(query.number(0)));
        entry->set_room_sequence(sequence);
        entry->set_server_timestamp_unix_ms(static_cast<std::uint64_t>(query.number(5)));
        result.set_next_sequence(sequence);
    }
    result.set_has_more(result.next_sequence() < through);
    if (result.has_more() && static_cast<std::uint32_t>(result.entries_size()) != limit) {
        throw std::invalid_argument("sync gap");
    }
    return result;
}
protocol::DirectRoomsResponse
HistoryStore::direct_rooms(std::int64_t principal, std::uint64_t after, std::uint32_t limit,
                           const std::vector<std::int64_t>& connected) {
    protocol::DirectRoomsResponse result;
    result.set_next_room_id(after);
    Statement query(
        impl_->db,
        "SELECT r.id,u.name,r.last_sequence,u.id FROM rooms r JOIN users u "
        "ON u.id=CASE WHEN r.a=? THEN r.b ELSE r.a END WHERE r.kind=0 AND (r.a=? OR r.b=?) "
        "AND r.id>? ORDER BY r.id LIMIT ?");
    query.bind(1, principal);
    query.bind(2, principal);
    query.bind(3, principal);
    query.bind(4, static_cast<std::int64_t>(after));
    query.bind(5, static_cast<std::int64_t>(limit) + 1);
    while (query.row()) {
        if (static_cast<std::uint32_t>(result.rooms_size()) == limit) {
            result.set_has_more(true);
            break;
        }
        auto* room = result.add_rooms();
        room->set_room_id("direct-" + std::to_string(query.number(0)));
        room->set_peer(query.text(1));
        room->set_last_sequence(static_cast<std::uint64_t>(query.number(2)));
        room->set_peer_connected(std::find(connected.begin(), connected.end(), query.number(3)) !=
                                 connected.end());
        result.set_next_room_id(static_cast<std::uint64_t>(query.number(0)));
    }
    return result;
}
} // namespace chat::server
