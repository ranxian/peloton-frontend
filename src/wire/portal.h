#include <sqlite3.h>
#include <string>

struct Portal {
  std::string portal_name;
  std::string query_string;
  sqlite3_stmt *stmt;
};
