#include "db_manager.h"
#include <iostream>

// ---------------------------------------------------------------
// db_manager.cc
//
// This file implements the methods defined in db_manager.h.
// It uses the SQLite C API to perform database operations.
// ---------------------------------------------------------------

// Constructor: Save the database filename and set the database pointer to nullptr.
DBManager::DBManager(const std::string& db_file)
    : db_file_(db_file), db_(nullptr) {
  // No database connection is made here; it is done in initialize().
}

// Destructor: Close the SQLite database connection if it is open.
DBManager::~DBManager() {
  if (db_) {
    sqlite3_close(db_);
  }
}

// initialize():
// Opens a connection to the SQLite database and creates necessary tables.
// If the database file doesn't exist, SQLite will create it.
bool DBManager::initialize() {
  int result = sqlite3_open(db_file_.c_str(), &db_);
  if (result != SQLITE_OK) {
    std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
    return false;
  }

  // SQL command to create a table for storing messages, if it doesn't already exist.
  // The table "messages" has an auto-incrementing primary key and a text column for the message content.
  std::string sql_create_table =
      "CREATE TABLE IF NOT EXISTS messages ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "content TEXT);";

  // Execute the SQL command using executeNonQuery.
  if (!executeNonQuery(sql_create_table)) {
    std::cerr << "Failed to create table." << std::endl;
    return false;
  }
  return true;
}

// executeNonQuery():
// Executes an SQL statement that does not expect to return data.
// Useful for commands like INSERT, UPDATE, DELETE, or CREATE TABLE.
bool DBManager::executeNonQuery(const std::string& sql) {
  char* errMsg = nullptr;
  int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
  if (result != SQLITE_OK) {
    std::cerr << "SQL error: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

// executeQuery():
// Executes an SQL SELECT statement that returns data.
// A callback function is used to process each row in the result.
// Here, the callback simply prints the column names and values.
bool DBManager::executeQuery(const std::string& sql) {
  // Callback function to process query results.
  auto callback = [](void* NotUsed, int argc, char** argv, char** azColName) -> int {
    for (int i = 0; i < argc; i++) {
      // Print the name of the column and its corresponding value.
      std::cout << azColName[i] << ": " << (argv[i] ? argv[i] : "NULL") << "\n";
    }
    std::cout << "----\n";
    return 0;
  };

  char* errMsg = nullptr;
  int result = sqlite3_exec(db_, sql.c_str(), callback, nullptr, &errMsg);
  if (result != SQLITE_OK) {
    std::cerr << "SQL error: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}
