#include "Database.h"

Database::Database( const std::string &path ) : m_path( path ) {
    LOG_INFO << "打开数据库: " << path;
    int rc = sqlite3_open( path.c_str(), &m_db );
    if ( rc != SQLITE_OK ) {
        LOG_ERROR << "打开数据库失败: " << sqlite3_errmsg( m_db );
        throw std::runtime_error( "Cannot open database: " + std::string( sqlite3_errmsg( m_db ) ) );
    }
    LOG_DEBUG << "数据库打开成功";
    // 启用外键约束和WAL日志模式，提高并发性能
    try {
        exec( "PRAGMA foreign_keys = ON;" );
        LOG_DEBUG << "已启用外键约束";
    } catch ( const std::exception &e ) {
        LOG_WARN << "启用外键约束失败: " << e.what();
    }
    try {
        exec( "PRAGMA journal_mode = WAL;" );
        LOG_DEBUG << "已启用WAL日志模式";
    } catch ( const std::exception &e ) {
        LOG_WARN << "启用WAL日志模式失败: " << e.what();
    }

    // 初始化数据库表结构
    initSchema();
    seedDefaultData();
}

Database::~Database() {
    LOG_DEBUG << "关闭数据库连接: " << m_path;
    if ( m_db )
        sqlite3_close( m_db );
}

void Database::initSchema() {
    LOG_DEBUG << "开始初始化数据库表结构 (schema.sql)";
    const unsigned char *schema = nullptr;
    int size = resource_get( "/sql/schema.sql", &schema );
    if ( size <= 0 ) {
        LOG_ERROR << "加载初始化sql文件 schema.sql 失败";
        return;
    }
    std::string schemaStr( schema, schema + size );
    try {
        exec( schemaStr );
        LOG_DEBUG << "数据库表结构初始化成功";
    } catch ( const std::exception &e ) {
        LOG_ERROR << "执行schema.sql失败: " << e.what();
        throw;
    }
}

void Database::seedDefaultData() {
    LOG_DEBUG << "开始填充默认数据...";
    auto rows = query( "SELECT id FROM users WHERE id='system'" );
    if ( rows.empty() ) {
        LOG_INFO << "创建默认系统用户 (system)";
        std::string now = utils::nowIso();
        execParams( "INSERT INTO users(id,email,nickname,password,created_at) VALUES(?,?,?,?,?)",
                    { { 1, "system" }, { 2, "system@clipboard.local" }, { 3, "" }, { 4, "system" }, { 5, now } } );
    }
    rows = query( "SELECT id FROM clipboard_teams WHERE id='default'" );
    if ( rows.empty() ) {
        LOG_INFO << "创建默认公共团队 (default)";
        std::string now = utils::nowIso();
        execParams( "INSERT INTO clipboard_teams(id,name,owner_id,is_default,created_at) VALUES(?,?,?,?,?)",
                    { { 1, "default" }, { 2, "公共团队" }, { 3, "system" }, { 4, "1" }, { 5, now } } );
    }
    LOG_DEBUG << "默认数据填充完成";
}

bool Database::migration( const std::string &column, const std::string &defaultValue ) {
    try {
        auto cols = query( "PRAGMA table_info(proc_configs)" );
        bool hasRemarks = false;
        for ( auto &c : cols ) {
            if ( c["name"] == column ) {
                hasRemarks = true;
                break;
            }
        }
        if ( !hasRemarks ) {
            LOG_INFO << "迁移数据库：添加 " << column << " 列到 proc_configs";
            exec( "ALTER TABLE proc_configs ADD COLUMN " + column + " TEXT DEFAULT '" + defaultValue + "'" );
        }
        return true;
    } catch ( const std::exception &e ) {
        LOG_WARN << "数据库迁移 remarks 列失败: " << e.what();
        return false;
    }
}

int Database::exec( const std::string &sql ) {
    std::lock_guard<std::mutex> lock( m_mtx );
    LOG_DEBUG << "SQL exec: " << ( sql.size() > 120 ? sql.substr( 0, 120 ) + "..." : sql );
    char *err = nullptr;
    int rc = sqlite3_exec( m_db, sql.c_str(), nullptr, nullptr, &err );
    if ( err ) {
        std::string e = err;
        sqlite3_free( err );
        if ( rc != SQLITE_OK ) {
            LOG_ERROR << "SQL执行错误: " << e;
            throw std::runtime_error( "SQL error: " + e );
        }
    }
    int changes = sqlite3_changes( m_db );
    LOG_DEBUG << "SQL执行完成,影响行数: " << changes;
    return changes;
}

Database::Rows Database::query( const std::string &sql ) {
    std::lock_guard<std::mutex> lock( m_mtx );
    LOG_DEBUG << "SQL query: " << ( sql.size() > 120 ? sql.substr( 0, 120 ) + "..." : sql );
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2( m_db, sql.c_str(), -1, &stmt, nullptr );
    if ( rc != SQLITE_OK ) {
        std::string err = sqlite3_errmsg( m_db );
        LOG_ERROR << "SQL prepare错误: " << err;
        throw std::runtime_error( "Prepare error: " + std::string( sqlite3_errmsg( m_db ) ) );
    }
    std::vector<Row> rows;
    int ncol = sqlite3_column_count( stmt );
    while ( sqlite3_step( stmt ) == SQLITE_ROW ) {
        Row row;
        for ( int i = 0; i < ncol; ++i ) {
            const char *name = sqlite3_column_name( stmt, i );
            const unsigned char *val = sqlite3_column_text( stmt, i );
            row[name] = val ? reinterpret_cast<const char *>( val ) : "";
        }
        rows.push_back( std::move( row ) );
    }
    sqlite3_finalize( stmt );
    LOG_DEBUG << "SQL查询完成,返回行数: " << rows.size();
    return rows;
}

int Database::execParams( const std::string &sql, const std::vector<std::pair<int, std::string>> &params ) {
    std::lock_guard<std::mutex> lock( m_mtx );
    LOG_DEBUG << "SQL execParams: " << ( sql.size() > 120 ? sql.substr( 0, 120 ) + "..." : sql )
              << " (参数数量: " << params.size() << ")";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2( m_db, sql.c_str(), -1, &stmt, nullptr );
    if ( rc != SQLITE_OK ) {
        std::string err = sqlite3_errmsg( m_db );
        LOG_ERROR << "SQL prepare错误 (execParams): " << err;
        throw std::runtime_error( "Prepare error: " + std::string( sqlite3_errmsg( m_db ) ) );
    }
    for ( const auto &p : params ) {
        sqlite3_bind_text( stmt, p.first, p.second.c_str(), -1, SQLITE_TRANSIENT );
    }
    rc = sqlite3_step( stmt );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) {
        std::string e = sqlite3_errmsg( m_db );
        LOG_ERROR << "SQL step错误 (execParams): " << e;
        sqlite3_finalize( stmt );
        throw std::runtime_error( "Step error: " + e );
    }
    int changes = sqlite3_changes( m_db );
    sqlite3_finalize( stmt );
    LOG_DEBUG << "SQL execParams完成,影响行数: " << changes;
    return changes;
}

int Database::lastInsertRowId() {
    std::lock_guard<std::mutex> lock( m_mtx );
    return static_cast<int>( sqlite3_last_insert_rowid( m_db ) );
}

sqlite3 *Database::handle() {
    return m_db;
}

std::mutex &Database::mutex() {
    return m_mtx;
}

std::string Database::sqlEscape( const std::string &s ) {
    std::string out;
    out.reserve( s.size() + 8 );
    for ( char c : s ) {
        if ( c == '\'' )
            out += "''";
        else
            out += c;
    }
    return out;
}

Database::json::array_t Database::getAppConfig( const std::string &databasePath ) {
    LOG_INFO << "读取应用配置: 打开数据库" << databasePath;
    sqlite3 *db = nullptr;
    int rc = sqlite3_open( databasePath.c_str(), &db );
    if ( rc != SQLITE_OK ) {
        LOG_ERROR << "读取应用配置打开数据库失败: " << sqlite3_errmsg( db );
        throw std::runtime_error( "Cannot open database: " + std::string( sqlite3_errmsg( db ) ) );
    }

    const char *sql = "SELECT key, value FROM app_config";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2( db, sql, -1, &stmt, nullptr );
    if ( rc != SQLITE_OK ) {
        std::string err = sqlite3_errmsg( db );
        LOG_ERROR << "SQL prepare错误: " << err;
        throw std::runtime_error( "Prepare error: " + std::string( err ) );
    }
    json::array_t config;
    int ncol = sqlite3_column_count( stmt );
    while ( sqlite3_step( stmt ) == SQLITE_ROW ) {
        Row row;
        for ( int i = 0; i < ncol; ++i ) {
            const char *name = sqlite3_column_name( stmt, i );
            const unsigned char *val = sqlite3_column_text( stmt, i );
            row[name] = val ? reinterpret_cast<const char *>( val ) : "";
        }
        try {
            auto item = json::parse( row["value"] );
            config.push_back( item );
        } catch ( const std::exception &e ) {
            LOG_ERROR << "解析配置项失败: " << e.what();
        }
    }
    sqlite3_finalize( stmt );
    LOG_DEBUG << "读取 app_config 成功，字段数: " << config.size();

    sqlite3_close( db );
    return config;
}

bool Database::saveAppConfig( const json::array_t &config ) {
    std::string now = utils::nowIso();
    for ( int i = 0; i < config.size(); i++ ) {
        auto item = config[i];
        std::string key = item["name"].get<std::string>();
        std::string value = item.dump();
        execParams( "INSERT OR REPLACE INTO app_config(key, value, updated_at) VALUES(?,?,?)",
                    {
                        { 1, key },
                        { 2, value },
                        { 3, now },
                    } );
    }
    LOG_INFO << "保存 app_config 完成，写入键数: " << config.size() << " updated_at=" << now;
    return true;
}

Database::json Database::getUserSettings( const std::string &userId ) {
    LOG_DEBUG << "读取用户设置 user_id=" << userId;
    auto rows = query(
        "SELECT key, value FROM user_settings WHERE user_id='" + sqlEscape( userId ) + "'" );
    json j = json::object();
    for ( auto &r : rows ) {
        if ( r.count( "key" ) && r.count( "value" ) )
            j[r["key"]] = r["value"];
    }
    LOG_DEBUG << "读取用户设置成功，字段数: " << j.size();
    return j;
}

bool Database::setUserSetting( const std::string &userId, const std::string &key, const std::string &value ) {
    std::string now = utils::nowIso();
    int rc = execParams(
        "INSERT OR REPLACE INTO user_settings(user_id, key, value, updated_at) VALUES(?,?,?,?)",
        { { 1, userId }, { 2, key }, { 3, value }, { 4, now } } );
    LOG_DEBUG << "写入用户设置 user_id=" << userId << " key=" << key << " rc=" << rc;
    return rc >= 0;
}

bool Database::deleteUserSetting( const std::string &userId, const std::string &key ) {
    int rc = execParams(
        "DELETE FROM user_settings WHERE user_id=? AND key=?",
        { { 1, userId }, { 2, key } } );
    LOG_DEBUG << "删除用户设置 user_id=" << userId << " key=" << key << " rc=" << rc;
    return rc >= 0;
}
