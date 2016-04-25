//
// Created by Siddharth Santurkar on 31/3/16.
//

#include "marshall.h"
#include "cache.h"
#include "portal.h"
#include "cache_entry.h"
#include "types.h"
#include <stdio.h>
#include <boost/algorithm/string.hpp>
#include <unordered_map>

#define PROTO_MAJOR_VERSION(x) x >> 16

namespace peloton {
namespace wire {

thread_local Cache<std::string, CacheEntry> cache_;
  // TODO: Should be replaced by dedicated class
thread_local std::unordered_map<std::string, std::shared_ptr<Portal>> portals_;

const std::unordered_map<std::string, std::string> PacketManager::parameter_status_map =
  boost::assign::map_list_of ("application_name","psql")("client_encoding", "UTF8")
  ("DateStyle", "ISO, MDY")("integer_datetimes", "on")
  ("IntervalStyle", "postgres")("is_superuser", "on")
  ("server_encoding", "UTF8")("server_version", "9.5devel")
  ("session_authorization", "postgres")("standard_conforming_strings", "on")
  ("TimeZone", "US/Eastern");

void print_packet(Packet* pkt UNUSED) {
  //if (pkt->msg_type) {
    //LOG_INFO("MsgType: %d", pkt->msg_type);
  //}

  //LOG_INFO("Len: %zu", pkt->len);

  //LOG_INFO("BufLen: %zu", pkt->buf.size());

  //LOG_INFO("{");
  //for (auto ele : pkt->buf) {
    //LOG_INFO("%d,", ele);
  //}
  //LOG_INFO("}\n");
}

void print_uchar_vector(std::vector<uchar>& vec){
  LOG_INFO("{");
  for (auto ele : vec) {
    LOG_INFO("%d (%c)", ele, ele);
  }
  LOG_INFO("}\n");
}

/*
 * close_client - Close the socket of the underlying client
 */
void PacketManager::close_client() { client.sock->close_socket(); }


void PacketManager::make_hardcoded_parameter_status(ResponseBuffer &responses, const std::pair<std::string, std::string>& kv) {
  std::unique_ptr<Packet> response(new Packet());
  response->msg_type = 'S';
  packet_putstring(response, kv.first);
  packet_putstring(response, kv.second);
  responses.push_back(std::move(response));
}
/*
 * process_startup_packet - Processes the startup packet
 * 	(after the size field of the header).
 */
bool PacketManager::process_startup_packet(Packet* pkt,
                                           ResponseBuffer& responses) {
  std::string token, value;
  std::unique_ptr<Packet> response(new Packet());

  int32_t proto_version = packet_getint(pkt, sizeof(int32_t));

  if (PROTO_MAJOR_VERSION(proto_version) != 3) {
    LOG_ERROR("Protocol error: Only protocol version 3 is supported.");
    exit(EXIT_FAILURE);
  }

  // TODO: check for more malformed cases
  // iterate till the end
  for (;;) {
    // loop end case?
    if (pkt->ptr >= pkt->len) break;
    token = get_string_token(pkt);

    // if the option database was found
    if (token.compare("database") == 0) {
      // loop end?
      if (pkt->ptr >= pkt->len) break;
      client.dbname = get_string_token(pkt);
    } else if (token.compare(("user")) == 0) {
      // loop end?
      if (pkt->ptr >= pkt->len) break;
      client.user = get_string_token(pkt);
    } else {
      if (pkt->ptr >= pkt->len) break;
      value = get_string_token(pkt);
      client.cmdline_options[token] = value;
    }
  }

  // send auth-ok
  response->msg_type = 'R';
  packet_putint(response, 0, 4);
  responses.push_back(std::move(response));

  for(auto it = parameter_status_map.begin(); it != parameter_status_map.end(); it++) {
    make_hardcoded_parameter_status(responses, *it);
  }

  send_ready_for_query(TXN_IDLE, responses);
  return true;
}

void PacketManager::put_row_desc(std::vector<wiredb::FieldInfoType> &rowdesc, ResponseBuffer &responses) {
  if (!rowdesc.size())
    return;

  std::unique_ptr<Packet> pkt(new Packet());
  pkt->msg_type = 'T';
  packet_putint(pkt, rowdesc.size(), 2);

  for(auto col : rowdesc) {
    packet_putstring(pkt, std::get<0>(col));
    packet_putint(pkt, 0, 4);
    packet_putint(pkt, 0, 2);
    packet_putint(pkt, std::get<1>(col), 4);
    packet_putint(pkt, std::get<2>(col), 2);
    packet_putint(pkt, -1, 4);
    // format code for text
    packet_putint(pkt, 0, 2);
  }
  responses.push_back(std::move(pkt));
}

void PacketManager::send_data_rows(std::vector<wiredb::ResType> &results, int colcount, ResponseBuffer &responses) {
  if (!results.size() || !colcount)
    return;

  size_t numrows = results.size() / colcount;

  // 1 packet per row
  for(size_t i = 0; i < numrows; i++) {
    std::unique_ptr<Packet> pkt(new Packet());
    pkt->msg_type = 'D';
    packet_putint(pkt, colcount, 2);
    for (int j = 0; j < colcount; j++) {
      packet_putint(pkt, results[i*colcount + j].second.size(), 4);
      packet_putbytes(pkt, results[i*colcount + j].second);
    }
    responses.push_back(std::move(pkt));
  }
}

/*
 * put_dummy_row_desc - Prepare a dummy row description packet
 */
void PacketManager::put_dummy_row_desc(ResponseBuffer& responses) {
  std::unique_ptr<Packet> pkt(new Packet());
  pkt->msg_type = 'T';
  packet_putint(pkt, 5, 2);

  for (int i = 0; i < 5; i++) {
    auto field = "F" + std::to_string(i);
    packet_putstring(pkt, field);
    packet_putint(pkt, 0, 4);
    packet_putint(pkt, 0, 2);
    packet_putint(pkt, 0, 4);
    packet_putint(pkt, 10, 2);
    packet_putint(pkt, -1, 4);
    // format code for text
    packet_putint(pkt, 0, 2);
  }

  responses.push_back(std::move(pkt));
}

void PacketManager::put_dummy_data_row(int colcount, int start,
                                       ResponseBuffer& responses) {
  std::unique_ptr<Packet> pkt(new Packet());
  std::string data;
  pkt->msg_type = 'D';
  packet_putint(pkt, colcount, 2);
  for (int i = 0; i < colcount; i++) {
    data = "row" + std::to_string(start + i);

    // add 1 for null-terminator
    packet_putint(pkt, data.length() + 1, 4);

    packet_putstring(pkt, data);
  }

  responses.push_back(std::move(pkt));
}

void PacketManager::complete_command(std::string &query_type, int rows, ResponseBuffer& responses) {
  std::unique_ptr<Packet> pkt(new Packet());
  pkt->msg_type = 'C';
  std::string tag = query_type;
  if(!query_type.compare("BEGIN"))
    txn_state = TXN_BLOCK;
  else if(!query_type.compare("COMMIT"))
    txn_state = TXN_IDLE;
  else if(!query_type.compare("INSERT"))
    tag += " 0 " + std::to_string(rows);
  else if(!query_type.compare("ROLLBACK"))
    txn_state = TXN_IDLE;
  else
    tag += " " + std::to_string(rows);
  packet_putstring(pkt, tag);

  responses.push_back(std::move(pkt));
}

/*
* put_empty_query_response - Informs the client that an empty query was sent
*/
void PacketManager::send_empty_query_response(ResponseBuffer& responses) {
  std::unique_ptr<Packet> response(new Packet());
  response->msg_type = 'I';
  responses.push_back(std::move(response));
}

bool PacketManager::hardcoded_execute_filter() {
  // Skip SET
  if(!query_type.compare("SET"))
    return false;
  // skip duplicate BEGIN
  if (!query_type.compare("BEGIN") && txn_state == TXN_BLOCK)
    return false;
  // skip duplicate Commits
  if (!query_type.compare("COMMIT") && txn_state == TXN_IDLE)
    return false;
  // skip duplicate Rollbacks
  if (!query_type.compare("ROLLBACK") && txn_state == TXN_IDLE)
    return false;
  return true;
}

/*
 * process_packet - Main switch block; process incoming packets,
 *  Returns false if the seesion needs to be closed.
 */
bool PacketManager::process_packet(Packet* pkt, ResponseBuffer& responses) {

  switch (pkt->msg_type) {
    case 'Q': {
      std::string q_str = packet_getstring(pkt, pkt->len);
      LOG_INFO("Query Received: %s \n", q_str.c_str());

      std::vector<std::string> queries;
      boost::split(queries, q_str, boost::is_any_of(";"));

      // just a ';' sent
      if (queries.size() == 1) {
        send_empty_query_response(responses);
        send_ready_for_query(txn_state, responses);
        return true;
      }

      // iterate till before the trivial string after the last ';'
      for (auto query = queries.begin(); query != queries.end() - 1; query++) {
        if (query->empty()) {
          send_empty_query_response(responses);
          send_ready_for_query(TXN_IDLE, responses);
          return true;
        }

        std::vector<std::string> query_tokens;
        boost::split(query_tokens, *query, boost::is_any_of(" "), boost::token_compress_on);
        std::string query_type = query_tokens[0];

        std::vector<wiredb::ResType> results;
        std::vector<wiredb::FieldInfoType> rowdesc;
        std::string err_msg;
        int rows_affected;

        int isfailed =  db.PortalExec(query->c_str(), results, rowdesc, rows_affected, err_msg);

        if(isfailed) {
          send_error_response({{'M', err_msg}}, responses);
          break;
        }

        put_row_desc(rowdesc, responses);

        send_data_rows(results, rowdesc.size(), responses);

        complete_command(query_type, rows_affected, responses);
      }

      // send_error_response({{'M', "Syntax error"}}, responses);
      send_ready_for_query('I', responses);
      break;
    }

    case 'P': {
      // PARSE message
      LOG_INFO("PARSE message");
      std::string err_msg;
      std::string prep_stmt_name = get_string_token(pkt);

      // Read prepare statement name
      sqlite3_stmt *stmt = nullptr;
      LOG_INFO("Prep stmt: %s", prep_stmt_name.c_str());
      // Read query string
      std::string query = get_string_token(pkt);
      LOG_INFO("Query: %s", query.c_str());

      // Workaround to bypass SET, BEGIN, ROLLBACK and COMMIT
      std::vector<std::string> query_tokens;
      boost::split(query_tokens, query, boost::is_any_of(" "),
                   boost::token_compress_on);
      query_type = query_tokens[0];
      skipped_stmt_ = false;
      if (!hardcoded_execute_filter()) {
        skipped_stmt_ = true;
        LOG_INFO("Statement skipped");
        std::unique_ptr<Packet> response(new Packet());
        // Send Parse complete response
        response->msg_type = '1';
        responses.push_back(std::move(response));
        break;
      }

      // Read number of params
      int num_params = packet_getint(pkt, 2);
      LOG_INFO("NumParams: %d", num_params);

      // Read param types
      std::vector<int32_t> param_types(num_params);
      for (int i = 0; i < num_params; i++) {
        int param_type = packet_getint(pkt, 4);
        param_types[i] = param_type;
      }

      // Prepare statement
      bool is_failed = db.PrepareStmt(query.c_str(), &stmt, err_msg);
      if (is_failed) {
        send_error_response({{'M', err_msg}}, responses);
        send_ready_for_query(txn_state, responses);
      }
      std::shared_ptr<CacheEntry> entry(new CacheEntry());
      entry->stmt_name = prep_stmt_name;
      entry->query_string = query;
      entry->sql_stmt = stmt;
      entry->param_types = std::move(param_types);

      if (prep_stmt_name.empty()) {
        // Unnamed statement
        unnamed_entry = entry;
      } else {
        cache_.insert(std::make_pair(std::move(prep_stmt_name), entry));
      }

      std::unique_ptr<Packet> response(new Packet());

      // Send Parse complete response
      response->msg_type = '1';
      responses.push_back(std::move(response));
      break;
    }

    case 'B': {
      // BIND message
      LOG_INFO("BIND message");
      std::string portal_name = get_string_token(pkt);
      LOG_INFO("Portal name: %s", portal_name.c_str());
      std::string prep_stmt_name = get_string_token(pkt);
      LOG_INFO("Prep stmt name: %s", prep_stmt_name.c_str());

      if (skipped_stmt_) {
        //send bind complete
        std::unique_ptr<Packet> response(new Packet());
        response->msg_type = '2';
        responses.push_back(std::move(response));
        break;
      }

      // Read parameter format
      int num_params_format = packet_getint(pkt, 2);

      std::vector<int16_t> formats(num_params_format);
      for (int i = 0; i < num_params_format; i++) {
        formats[i] = packet_getint(pkt, 2);
      }

      int num_params = packet_getint(pkt, 2);
      if (num_params_format != num_params) {
        // TODO: Should throw error here.
      }

      // Get statement info generated in PARSE message
      sqlite3_stmt *stmt = nullptr;
      std::string query_string;
      std::shared_ptr<CacheEntry> entry;
      if (prep_stmt_name.empty()) {
        entry = unnamed_entry;
      } else {
        auto itr = cache_.find(prep_stmt_name);
        if (itr != cache_.end()) {
          entry = *itr;
        } else {
          // TODO: Should throw error here
        }
      }
      stmt = entry->sql_stmt;
      query_string = entry->query_string;

      std::vector<std::pair<int, std::string>> bind_parameters;
      for (int param_idx = 0; param_idx < num_params; param_idx++) {
        int param_len = packet_getint(pkt, 4);
        auto param = packet_getbytes(pkt, param_len);

        if (formats[param_idx] == 0) {
          // TEXT mode
          LOG_INFO("TEXT mode");
          std::string param_str = std::string(std::begin(param),
                                              std::end(param));
          bind_parameters.push_back(std::make_pair(WIRE_TEXT, param_str));
          LOG_INFO("Bind param (size: %d) : %s", param_len, param_str.c_str());
        } else {
          // BINARY mode
          LOG_INFO("BINARY mode");
          switch (entry->param_types[param_idx]) {
            case POSTGRES_VALUE_TYPE_INTEGER: {
              int int_val = 0;
              for (size_t i = 0; i < sizeof(int); ++i) {
                int_val = (int_val << 8) | param[i];
              }
              bind_parameters.push_back(
                  std::make_pair(WIRE_INTEGER, std::to_string(int_val)));
              LOG_INFO("Bind param (size: %d) : %d", param_len, int_val);
            } break;
            case POSTGRES_VALUE_TYPE_DOUBLE: {
              double float_val = 0;
              unsigned long buf = 0;
              for (size_t i = 0; i < sizeof(double); ++i) {
                buf = (buf << 8) | param[i];
              }
              memcpy(&float_val, &buf, sizeof(double));
              // TODO: read float val
              bind_parameters.push_back(
                  std::make_pair(WIRE_FLOAT, std::to_string(float_val)));
              LOG_INFO("Bind param (size: %d) : %lf", param_len, float_val);
            } break;
            default: {
              LOG_ERROR("Do not support data type: %d",
                        entry->param_types[param_idx]);
            } break;
          }
        }
      }

      std::string err_msg;
      bool is_failed = db.BindStmt(bind_parameters, &stmt, err_msg);
      if (is_failed) {
        send_error_response({{'M', err_msg}}, responses);
        send_ready_for_query(txn_state, responses);
      }

      std::shared_ptr<Portal> portal(new Portal());
      portal->query_string = std::move(query_string);
      portal->stmt = stmt;
      portal->portal_name = std::move(portal_name);

      auto itr = portals_.find(portal_name);
      if (portals_.find(portal_name) == portals_.end()) {
        portals_.insert(std::make_pair(portal_name, portal));
      } else {
        std::shared_ptr<Portal> p = itr->second;
        itr->second = portal;
      }

      //send bind complete
      std::unique_ptr<Packet> response(new Packet());
      response->msg_type = '2';
      responses.push_back(std::move(response));
      break;
    }

    case 'D': {
      auto mode = packet_getbytes(pkt, 1);

      LOG_INFO("CASE D reached. DO nothing");
      break;
    }

    case 'E': {
      // EXECUTE message
      LOG_INFO("EXECUTE message");
      std::vector<wiredb::ResType> results;
      std::vector<wiredb::FieldInfoType> rowdesc;
      std::string err_msg;
      sqlite3_stmt *stmt = nullptr;
      int rows_affected = 0, is_failed;
      std::string portal_name = get_string_token(pkt);

      // covers weird JDBC edge case of sending double BEGIN statements. Don't execute them
      if (skipped_stmt_) {
        LOG_INFO("Statement skipped");
        complete_command(query_type, rows_affected, responses);
        break;
      }

      auto portal = portals_[portal_name];
      stmt = portal->stmt;
      ASSERT(stmt != nullptr);

      LOG_INFO("Executing query: %s", portal->query_string.c_str());
      is_failed = db.ExecPrepStmt(stmt, results, rowdesc, rows_affected,
                                  err_msg);
      if (is_failed) {
        send_error_response({{'M', err_msg}}, responses);
        send_ready_for_query(txn_state, responses);
      }

      put_row_desc(rowdesc, responses);
      send_data_rows(results, rowdesc.size(), responses);
      complete_command(query_type, rows_affected, responses);

      break;
    }

    case 'S': {
      // SYNC message
      send_ready_for_query(txn_state, responses);
      break;
    }

    case 'X':
      LOG_INFO("Closing client");
      return false;

    default:
      LOG_INFO("Packet type not supported yet: %d (%c)", pkt->msg_type, pkt->msg_type);
  }
  return true;
}

/*
 * send_error_response - Sends the passed string as an error response.
 * 		For now, it only supports the human readable 'M' message body
 */
void PacketManager::send_error_response(
    std::vector<std::pair<uchar, std::string>> error_status,
    ResponseBuffer& responses) {
  std::unique_ptr<Packet> pkt(new Packet());
  pkt->msg_type = 'E';

  for (auto entry : error_status) {
    packet_putbyte(pkt, entry.first);
    packet_putstring(pkt, entry.second);
  }

  // put null terminator
  packet_putbyte(pkt, 0);

  // don't care if write finished or not, we are closing anyway
  responses.push_back(std::move(pkt));
}

void PacketManager::send_ready_for_query(uchar txn_status,
                                         ResponseBuffer& responses) {
  std::unique_ptr<Packet> pkt(new Packet());
  pkt->msg_type = 'Z';

  packet_putbyte(pkt, txn_status);

  responses.push_back(std::move(pkt));
}

/*
 * PacketManager - Main wire protocol logic.
 * 		Always return with a closed socket.
 */
void PacketManager::manage_packets() {
  Packet pkt;
  ResponseBuffer responses;
  bool status;

  // fetch the startup packet
  if (!read_packet(&pkt, false, &client)) {
    close_client();
    return;
  }

  print_packet(&pkt);
  status = process_startup_packet(&pkt, responses);
  if (!write_packets(responses, &client) || !status) {
    // close client on write failure or status failure
    close_client();
    return;
  }

  pkt.reset();
  while (read_packet(&pkt, true, &client)) {
    print_packet(&pkt);
    status = process_packet(&pkt, responses);
    if (!write_packets(responses, &client) || !status) {
      // close client on write failure or status failure
      close_client();
      return;
    }
    pkt.reset();
  }
}
}
}
