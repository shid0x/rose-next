#include "rose/database/database.h"
#include "rose/common/util.h"

#include <libpq-fe.h>

using namespace Rose::Util;

namespace Rose::Database {
const int POSTGRES_TEXT_FORMAT = 0;
const int POSTGRES_BINARY_FORMAT = 1;

std::string
param_list(size_t count) {
    std::string s;
    for (size_t i = 1; i <= count; ++i) {
        if (i != 1) {
            s += ", ";
        }
        s += "$" + std::to_string(i);
    }
    return s;
}

std::string
value_list(std::vector<std::string>& vals, bool quote) {
    // User is likely using this with the `IN` operator so we ensure that that
    // a vald value list is alway returned.
    // E.g. `... IN (null)` vs the invalid syntax of `... IN ()`
    if (vals.size() == 0) {
        return "null";
    }

    std::string s;
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i != 0) {
            s += ", ";
        }
        if (quote) {
            s += "'" + vals[i] + "'";
        } else {
            s += vals[i];
        }
    }
    return s;
}

void
PgConnDeleter::operator()(PGconn* c) {
    PQfinish(c);
};

void
PgResultDeleter::operator()(PGresult* r) {
    PQclear(r);
};

/*
    Query Param
*/
QueryParam::QueryParam(): type(QueryParamType::Null){};

QueryParam::QueryParam(const std::string& s): type(QueryParamType::String) {
    data.assign(s.begin(), s.end());
    data.push_back(0);
}

QueryParam::QueryParam(const char* s): type(QueryParamType::String) {
    std::string str(s);
    data.assign(str.begin(), str.end());
    data.push_back(0);
}

/*
Query Result
*/
QueryResult::QueryResult(PGresult* res): res(res) {
    this->row_count = PQntuples(this->res.get());
    this->col_count = PQnfields(this->res.get());
    this->data.resize(this->row_count * this->col_count);

    for (size_t row_idx = 0; row_idx < this->row_count; ++row_idx) {
        for (size_t col_idx = 0; col_idx < this->col_count; ++col_idx) {
            size_t val_idx = (row_idx * this->col_count) + col_idx;
            this->data[val_idx] = PQgetvalue(this->res.get(), row_idx, col_idx);
        }
    }
};

bool
QueryResult::is_ok() {
    const ExecStatusType status = PQresultStatus(this->res.get());
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

const char*
QueryResult::error_message() {
    return PQresultErrorMessage(this->res.get());
}

QueryResultRows
QueryResult::rows() {
    QueryResultRows rows;
    rows.reserve(this->row_count);

    for (size_t row_idx = 0; row_idx < this->row_count; ++row_idx) {
        const size_t row_start_idx = row_idx * this->col_count;

        auto row_begin = this->data.begin() + row_start_idx;
        auto row_end = row_begin + this->col_count;

        rows.push_back(this->row(row_idx));
    }

    return rows;
}

QueryResultRow
QueryResult::row(size_t row_idx) {
    const size_t row_start_idx = row_idx * this->col_count;
    auto row_begin = this->data.begin() + row_start_idx;
    auto row_end = row_begin + this->col_count;

    // Row index is invalid
    if (row_begin > this->data.end() || row_end > this->data.end()) {
        return QueryResultRow();
    }

    return QueryResultRow(row_begin, row_end);
}

std::string
QueryResult::get_string(size_t row_idx, size_t col_idx) {
    const size_t val_idx = (row_idx * this->col_count) + col_idx;
    if (val_idx >= this->data.size()) {
        return "";
    }
    return this->data.at(val_idx);
}

int32_t
QueryResult::get_int32(size_t row_idx, size_t col_idx) {
    try {
        return std::stoi(this->get_string(row_idx, col_idx));
    } catch (...) {
        return 0;
    }
}

int64_t
QueryResult::get_int64(size_t row_idx, size_t col_idx) {
    try {
        return std::stoll(this->get_string(row_idx, col_idx));
    } catch (...) {
        return 0;
    }
}

float
QueryResult::get_float(size_t row_idx, size_t col_idx) {
    try {
        return std::stof(this->get_string(row_idx, col_idx));
    } catch (...) {
        return 0.0f;
    }
}

double
QueryResult::get_double(size_t row_idx, size_t col_idx) {
    try {
        return std::stod(this->get_string(row_idx, col_idx));
    } catch (...) {
        return 0.0;
    }
}

bool
QueryResult::get_bool(size_t row_idx, size_t col_idx) {
    bool res = this->get_string(row_idx, col_idx) == "t";
    return res;
}

DateTime
QueryResult::get_datetime(size_t row_idx, size_t col_idx) {
    return parse_datetime_str(this->get_string(row_idx, col_idx)).value_or(DateTime());
}

bool
QueryResult::get_null(size_t row_idx, size_t col_idx) {
    return this->get_string(row_idx, col_idx).empty();
}

/*
    Client
*/
bool
Client::connect(const std::string& conn_string) {
    this->conn = PgConnPtr{PQconnectdb(conn_string.c_str())};

    if (PQstatus(this->conn.get()) != CONNECTION_OK) {
        return false;
    }

    return true;
}

const char*
Client::last_error_message() {
    return PQerrorMessage(this->conn.get());
}

QueryResult
Client::query(const std::string& statement, const std::vector<std::string>& params) {
    std::vector<const char*> c_params;
    for (const std::string& s: params) {
        c_params.push_back(s.c_str());
    }

    PGresult* pg_res = PQexecParams(this->conn.get(),
        statement.c_str(),
        params.size(),
        nullptr,
        c_params.data(),
        nullptr,
        nullptr,
        POSTGRES_TEXT_FORMAT);

    QueryResult res(pg_res);
    return res;
}

QueryResult
Client::queryb(const std::string& statement, const std::vector<QueryParam>& params) {
    std::vector<int> formats(params.size(), POSTGRES_TEXT_FORMAT);
    std::vector<const char*> data;
    std::vector<int> lengths;
    for (const QueryParam& param: params) {
        if (param.type == QueryParamType::Null) {
            data.push_back(nullptr);
            lengths.push_back(0);
            continue;
        }
        data.push_back(reinterpret_cast<const char*>(param.data.data()));
        lengths.push_back(param.data.size());
    }

    PGresult* pg_res = PQexecParams(this->conn.get(),
        statement.c_str(),
        params.size(),
        nullptr,
        data.data(),
        lengths.data(),
        formats.data(),
        POSTGRES_TEXT_FORMAT);

    QueryResult res(pg_res);
    return res;
}

QueryResult
Client::batch(const std::string& statement) {
    return QueryResult(PQexec(this->conn.get(), statement.c_str()));
}

} // namespace Rose::Database