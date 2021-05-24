#pragma once

#include <algorithm>
#include <cassert>

#include "concurrency_manager.hpp"
#include "database.hpp"

class Transaction {
public:
    Transaction() = default;

    void abort() {
        // do nothing
    }

    enum Result {
        SUCCESS,
        FAIL,  // e.g. not found, already exists -> user abort
        ABORT  // e.g. could not acquire lock/latch and no-wait-> system abort
    };

    template <typename Record>
    Result get_record(Record& rec, typename Record::Key key) {
        if (!cm.slock()) {
            abort();
            return Result::ABORT;
        }
        // look up into database
        if (db.get_record<Record>(rec, key)) {
            return Result::SUCCESS;
        } else {
            return Result::FAIL;
        }
    }

    template <typename Record>
    Result insert_record(const Record& rec) {
        if (!cm.xlock()) {
            abort();
            return Result::ABORT;
        }
        typename Record::Key key = Record::Key::create_key(rec);
        if (db.insert_record<Record>(rec, key)) {
            return Result::SUCCESS;
        } else {
            return Result::FAIL;
        }
    }

    template <typename Record>
    Result update_record(typename Record::Key key, const Record& rec) {
        if (!cm.xlock()) {
            abort();
            return Result::ABORT;
        }
        if (db.update_record<Record>(key, rec)) {
            return Result::SUCCESS;
        } else {
            return Result::FAIL;
        }
    }

    template <typename Record>
    Result delete_record(typename Record::Key key) {
        if (!cm.xlock()) {
            abort();
            return Result::ABORT;
        }
        if (db.delete_record<Record>(key)) {
            return Result::SUCCESS;
        } else {
            return Result::FAIL;
        }
    }

    Result get_customer_by_last_name(Customer& c, CustomerSecondary::Key c_sec_key) {
        if (!cm.slock()) {
            abort();
            return Result::ABORT;
        }
        auto low_iter = db.get_lower_bound_iter<CustomerSecondary>(c_sec_key);
        auto up_iter = db.get_upper_bound_iter<CustomerSecondary>(c_sec_key);

        if (low_iter == up_iter) return Result::FAIL;
        int n = std::distance(low_iter, up_iter);

        std::vector<CustomerSecondary> temp;
        temp.reserve(n);
        for (auto it = low_iter; it != up_iter; it++) {
            assert(it->second.ptr != nullptr);
            temp.push_back(it->second);
        }

        std::sort(
            temp.begin(), temp.end(),
            [](const CustomerSecondary& lhs, const CustomerSecondary& rhs) {
                return strncmp(lhs.ptr->c_first, rhs.ptr->c_first, Customer::MAX_FIRST) < 0;
            });

        c.deep_copy_from(*(temp[(n + 1) / 2 - 1].ptr));
        return Result::SUCCESS;
    }

    Result get_order_by_customer_id(Order& o, OrderSecondary::Key o_sec_key) {
        if (!cm.slock()) {
            abort();
            return Result::ABORT;
        }
        auto low_iter = db.get_lower_bound_iter<OrderSecondary>(o_sec_key);
        auto up_iter = db.get_upper_bound_iter<OrderSecondary>(o_sec_key);
        if (low_iter == up_iter) return Result::FAIL;

        uint32_t max_o_id = 0;
        Order* o_ptr = nullptr;
        for (auto it = low_iter; it != up_iter; it++) {
            assert(it->second.ptr != nullptr);
            if (it->second.ptr->o_id > max_o_id) {
                max_o_id = it->second.ptr->o_id;
                o_ptr = it->second.ptr;
            }
        }
        assert(o_ptr != nullptr);
        o.deep_copy_from(*o_ptr);
        return Result::SUCCESS;
    }

    Result get_neworder_with_smallest_key_no_less_than(NewOrder& no, NewOrder::Key low) {
        if (cm.slock()) {
            abort();
            return Result::ABORT;
        }
        auto low_iter = db.get_lower_bound_iter<NewOrder>(low);
        if (low_iter->first.w_id == low.w_id && low_iter->first.d_id == low.d_id) {
            no.deep_copy_from(low_iter->second);
            return Result::SUCCESS;
        } else {
            return Result::FAIL;
        }
    }

    // [low ,up)
    template <typename Record, typename Func>
    Result range_query(typename Record::Key low, typename Record::Key up, Func&& func) {
        if (cm.slock()) {
            abort();
            return Result::ABORT;
        }
        auto low_iter = db.get_lower_bound_iter<Record>(low);
        auto up_iter = db.get_lower_bound_iter<Record>(up);
        if (low_iter == up_iter) return Result::FAIL;
        for (auto it = low_iter; it != up_iter; it++) {
            Func(it->second);
        }
        return Result::SUCCESS;
    }

    // [low ,up)
    template <typename Record, typename Func>
    Result range_update(typename Record::Key low, typename Record::Key up, Func&& func) {
        if (cm.xlock()) {
            abort();
            return Result::ABORT;
        }
        auto low_iter = db.get_lower_bound_iter<Record>(low);
        auto up_iter = db.get_lower_bound_iter<Record>(up);
        if (low_iter == up_iter) return Result::FAIL;
        for (auto it = low_iter; it != up_iter; it++) {
            Func(it->second);
        }
        return Result::SUCCESS;
    }

private:
    Database& db;
    ConcurrencyManager cm;
};


template <>
inline Transaction::Result Transaction::insert_record<History>(const History& rec) {
    if (!cm.xlock()) {
        abort();
        return Result::ABORT;
    }
    if (db.insert_record<History>(rec)) {
        return Result::SUCCESS;
    } else {
        return Result::FAIL;
    }
}