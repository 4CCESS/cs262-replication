#ifndef DB_MANAGER_H
#define DB_MANAGER_H

// ---------------------------------------------------------------
// db_manager.h
//
// This header defines the DBManager class, which abstracts and
// encapsulates the interactions with an SQLite database.
//
// SQLite is a lightweight, file-based database engine. In this
// application, it is used to store chat messages or other data.
//
// The DBManager class provides functions for initializing the
// database, creating tables, and executing SQL commands.
// ---------------------------------------------------------------

#include <string>
#include <sqlite3.h>

// DBManager: A class that manages database operations.
// It wraps the low-level SQLite API calls into simple methods.
class DBManager {
 public:
  // Constructor:
  // Accepts the filename (path) of the SQLite database.
  DBManager(const std::string& db_file);

  // Destructor:
  // Ensures that the database connection is closed when the object is destroyed.
  ~DBManager();

  // initialize():
  // Opens the SQLite database and creates necessary tables if they do not exist.
  // Returns true if initialization is successful.
  bool initialize();

  // executeNonQuery():
  // Executes an SQL command that does not return data (e.g., INSERT, UPDATE, or CREATE TABLE).
  // Returns true if the SQL command executes successfully.
  bool executeNonQuery(const std::string& sql);

  // executeQuery():
  // Executes an SQL SELECT query that returns data.
  // For simplicity, this example prints the results to the console.
  // Returns true if the query executes successfully.
  bool executeQuery(const std::string& sql);

 private:
  // Pointer to the SQLite database connection object.
  sqlite3* db_;

  // The filename (and path) to the SQLite database file.
  std::string db_file_;
};

#endif // DB_MANAGER_H
