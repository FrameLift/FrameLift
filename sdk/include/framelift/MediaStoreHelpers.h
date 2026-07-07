#pragma once
#include <framelift/services/IMediaStore.h>

#include <cstddef>
#include <string>

// Ergonomic RAII wrapper over the ABI-safe IMediaStore service, compiled into the
// plugin (like JsonHelpers / ContextHelpers). Keeps call sites readable instead of
// juggling opaque void* handles. A failed Prepare yields an inert statement whose
// operations are safe no-ops, so call chains never crash.
namespace framelift
{

// One prepared statement — Prepare on construction, Finalize on destruction. Bound
// to the constructing thread (per the IMediaStore threading contract).
class SqlStmt
{
public:
    SqlStmt(IMediaStore& store, const char* sql) noexcept : store_(&store), stmt_(store.Prepare(sql))
    {
    }

    ~SqlStmt()
    {
        if (stmt_)
        {
            store_->Finalize(stmt_);
        }
    }

    SqlStmt(const SqlStmt&) = delete;
    SqlStmt& operator=(const SqlStmt&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return stmt_ != nullptr;
    }

    bool bind(int index, const char* value) noexcept
    {
        return stmt_ && store_->BindText(stmt_, index, value);
    }

    bool bind(int index, const std::string& value) noexcept
    {
        return bind(index, value.c_str());
    }

    bool bind(int index, double value) noexcept
    {
        return stmt_ && store_->BindDouble(stmt_, index, value);
    }

    bool bind(int index, long long value) noexcept
    {
        return stmt_ && store_->BindInt64(stmt_, index, value);
    }

    bool bindNull(int index) noexcept
    {
        return stmt_ && store_->BindNull(stmt_, index);
    }

    // 1 ⇒ row available, 0 ⇒ done, -1 ⇒ error (0 on an inert statement).
    [[nodiscard]] int step() noexcept
    {
        return stmt_ ? store_->Step(stmt_) : 0;
    }

    bool reset() noexcept
    {
        return stmt_ && store_->Reset(stmt_);
    }

    [[nodiscard]] std::string str(int col) const
    {
        if (!stmt_)
        {
            return {};
        }
        const int len = store_->ColumnText(stmt_, col, nullptr, 0);
        if (len <= 0)
        {
            return {};
        }
        std::string s(static_cast<std::size_t>(len), '\0');
        (void)store_->ColumnText(stmt_, col, s.data(), len + 1);
        return s;
    }

    [[nodiscard]] double num(int col) const noexcept
    {
        return stmt_ ? store_->ColumnDouble(stmt_, col) : 0.0;
    }

    [[nodiscard]] long long integer(int col) const noexcept
    {
        return stmt_ ? store_->ColumnInt64(stmt_, col) : 0;
    }

    [[nodiscard]] bool isNull(int col) const noexcept
    {
        return !stmt_ || store_->ColumnIsNull(stmt_, col);
    }

private:
    IMediaStore* store_;
    void* stmt_;
};

} // namespace framelift
