//
// Created by Zrs_y on 4/18/16.
//

#ifndef PELOTON_SQLITE_H
#define PELOTON_SQLITE_H

#include "database.h"
#include <stdlib.h>
#include <sqlite3.h>
#include <stdio.h>

namespace peloton {
namespace wiredb {

#define UNUSED __attribute__((unused))
class Sqlite : public DataBase {
public:
  Sqlite() {
    // sqlite3_open(filename, sqlite3 **db)
    // filename is null for in memory db
    auto rc = sqlite3_open("ycsb.db", &db);
    if (rc) {
      fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
      LOG_ERROR("Can't open database %s", sqlite3_errmsg(db));
      exit(0);
    } else {
      fprintf(stderr, "Opened database successfully\n");
    }

    // TODO: remove test
     test();
  }

  virtual ~Sqlite() {
    sqlite3_close(db);
  }

  /*
   * PortalExec - Execute query string
   */
  virtual int PortalExec(const char *query,
                         std::vector<ResType> &res,
                         std::vector<FieldInfoType> &info,
                         int &rows_change,
                         std::string &err_msg) {
    LOG_INFO("receive %s", query);
    sqlite3_stmt *sql_stmt;
    sqlite3_prepare_v2(db, query, -1, &sql_stmt, NULL);
    return ExecPrepStmt(sql_stmt, res, info, rows_change, err_msg);
  }
/*

  int PortalDesc(const char *table_name, std::vector<FieldInfoType> &field_info, std::string &err_msg) {

    char *zErrMsg = 0;

    std::string query = "PRAGMA table_info(" + std::string(table_name) + ");";
    LOG_INFO("describle query %s", query.c_str());
    auto rc = sqlite3_exec(db, query.c_str(), descCallback, (void *) &field_info, &zErrMsg);
    if (rc != SQLITE_OK) {
      LOG_INFO("error in des %s", zErrMsg);
      if (zErrMsg != NULL)
        err_msg = std::string(zErrMsg);
        sqlite3_free(zErrMsg);
      return 1;
    } else {
      return 0;
    }
  }
*/

  /*
   * InitBindPrepStmt - Prepare and bind a query from a query string
   */
  int PrepareStmt(const char *query, sqlite3_stmt **stmt, std::string &err_msg) {
    int rc = sqlite3_prepare_v2(db, query, -1, stmt, NULL);
    if (rc != SQLITE_OK) {
      err_msg = std::string(sqlite3_errmsg(db));
      return 1;
    }

    return 0;
  }

  int BindStmt(std::vector<std::pair<int, std::string>> &parameters,
               sqlite3_stmt **stmt, std::string &err_msg) {
    // BIND
    LOG_INFO("PARAMS SIZE: %zu", parameters.size());

    int paramno = 1;
    for (auto &param : parameters) {
      auto wire_type = param.first;
      auto &wire_val = param.second;
      int rc;
      switch (wire_type) {
        case WIRE_INTEGER: {
          int int_val = std::stoi(wire_val);
          LOG_INFO("BIND INT: %d", int_val);
          rc = sqlite3_bind_int(*stmt, paramno, int_val);
        } break;
        case WIRE_FLOAT: {
          double double_val = std::stod(wire_val);
          LOG_INFO("BIND FLOAT: %lf", double_val);
          rc = sqlite3_bind_double(*stmt, paramno, double_val);
        } break;
        case WIRE_TEXT: {
          const char *str_val = wire_val.c_str();
          size_t str_len = wire_val.size();
          LOG_INFO("BIND TEXT: %s", str_val);
          rc = sqlite3_bind_text(*stmt, paramno, str_val, (int) str_len, 0);
        } break;
        default: {
          LOG_INFO("Unknown bind type");
          return 1;
        }
      }
      if (rc != SQLITE_OK) {
        LOG_INFO("Error in binding: %s", sqlite3_errmsg(db));
        err_msg = std::string(sqlite3_errmsg(db));
        return 1;
      }
      paramno++;
    }

    return 0;
  }

  /*
   * ExecPrepStmt - Execute a statement from a prepared and bound statement
   */
  int ExecPrepStmt(void *stmt, std::vector<ResType> &res,
                   std::vector<FieldInfoType> &info, int &rows_change,
                   std::string &err_msg) {
    LOG_INFO("Executing statement......................");
    auto sql_stmt = (sqlite3_stmt *)stmt;
    auto ret = sqlite3_step(sql_stmt);
    auto col_num = sqlite3_column_count(sql_stmt);
    int first = true;
    while (ret == SQLITE_ROW) {
      for (int i = 0; i < col_num; i++) {
        int t = sqlite3_column_type(sql_stmt, i);
        const char *name = sqlite3_column_name(sql_stmt, i);
        std::string value;
        switch (t) {
          case SQLITE_INTEGER: {
            int v = sqlite3_column_int(sql_stmt, i);
            value = std::to_string(v);
            if(first)
              info.push_back(std::make_tuple(name, 23, 4));
            break;
          }
          case SQLITE_FLOAT: {
            float v = (float)sqlite3_column_double(sql_stmt, i);
            if(first)
              info.push_back(std::make_tuple(name, 700, 4));
            value = std::to_string(v);
            break;
          }
          case SQLITE_TEXT: {
            const char *v = (char *)sqlite3_column_text(sql_stmt, i);
            if(first)
              info.push_back(std::make_tuple(name, 25, 255));
            value = std::string(v);
            break;
          }
          default: break;
        }
        res.push_back(ResType());
        LOG_INFO("res from exeStmt %s %s\n", name, value.c_str());
        copyFromTo(name, res.back().first);
        copyFromTo(value.c_str(), res.back().second);
      }
      first = false;
      ret = sqlite3_step(sql_stmt);
    }

    sqlite3_reset(sql_stmt);
    sqlite3_db_release_memory(db);
    // sql_stmt = nullptr;
    if (ret != SQLITE_DONE) {
      LOG_INFO("ret num %d, err is %s", ret, sqlite3_errmsg(db));
      err_msg = std::string(sqlite3_errmsg(db));
      return 1;
    }
    rows_change = sqlite3_changes(db);
    return 0;
  }


private:
  void test() {
    LOG_INFO("RUN TEST");

    std::vector<ResType> res;
    std::vector<FieldInfoType> info;
    std::string err;
    int rows;


    // create table
    PortalExec("DROP TABLE IF EXISTS AA", res, info, rows, err);
    PortalExec("CREATE TABLE AA (id INT PRIMARY KEY, data TEXT);", res, info, rows, err);
    res.clear();

    // test simple insert
    PortalExec("INSERT INTO AA VALUES (1, 'abc'); ", res, info, rows, err);
    std::vector<std::pair<int, std::string>> parameters;
    parameters.push_back(std::make_pair(WIRE_TEXT, std::string("12")));
    parameters.push_back(std::make_pair(WIRE_TEXT, std::string("abc")));


    // test bind
    sqlite3_stmt *s;
    PrepareStmt("insert into AA (id, data) values ( ?, ? )", &s, err);
    BindStmt(parameters, &s, err);
    ExecPrepStmt(s, res, info, rows, err);
    BindStmt(parameters, &s, err);
    ExecPrepStmt(s, res, info, rows, err);
    res.clear();

    // select all
    sqlite3_stmt *sql_stmt;
    sqlite3_prepare_v2(db, "select * from AA;", -1, &sql_stmt, NULL);
    res.clear();
    info.clear();
    ExecPrepStmt(sql_stmt, res, info, rows, err);

    // res.size() should be 4
    // info.size() should be 2
    LOG_INFO("col %ld, info %ld", res.size(), info.size());

    for(auto item : res) {
      for(char c : item.first) {
        LOG_INFO("%c", c);
      }
      LOG_INFO("\n");
      for(char c : item.second) {
        LOG_INFO("%c", c);
      }
    }

    res.clear();
  }
  static inline void copyFromTo(const char *src, std::vector<unsigned char> &dst) {
    if (src == nullptr) {
      return;
    }
    LOG_INFO("ENTER: strlen: %zu", strlen(src));
    for(unsigned int i = 0; i < strlen(src); i++){
      dst.push_back((unsigned char)src[i]);
    }
    LOG_INFO("EXIT: strlen: %zu", strlen(src));
  }

  static int execCallback(void *res, int argc, char **argv, char **azColName){
    auto output = (std::vector<ResType> *)res;
    for(int i = 0; i < argc; i++){
      output->push_back(ResType());
      if (argv[i] == NULL) {
        LOG_INFO("value is null");
      }else if(azColName[i] == NULL) {
        LOG_INFO("name is null");
      }else {
        LOG_INFO("res %s %s", azColName[i], argv[i]);
      }
      copyFromTo(azColName[i], output->at(i).first);
      copyFromTo(argv[i], output->at(i).second);
    }

    return 0;
  }

  static int getSize(const std::string& type UNUSED) {
    return 0;
  }
  /*
  static int descCallback(void *res, int argc, char **argv, char **azColName){
    auto output = (std::vector<FieldInfoType> *)res;
    std::string name, type;
    for(int i = 0; i < argc; i++){
      output->push_back(FieldInfoType());
      if (argv[i] == NULL) {
        LOG_INFO("value is null");
      }else if(azColName[i] == NULL) {
        LOG_INFO("name is null");
      }else {
        if(strcmp(azColName[i], "name") == 0) {
          LOG_INFO("name is %s", argv[i]);
          name = std::string(argv[i]);
        }else if(strcmp(azColName[i], "type") == 0) {
          LOG_INFO("type is %s", argv[i]);
          type = std::string(argv[i]);
        }
      }

      int size = getSize(type);
      output->push_back(std::make_tuple(name, type, size));
      return 0;
    }

    return 0;
  }
   */

private:
  sqlite3 *db;
};
}
}

#endif //PELOTON_SQLITE_H
