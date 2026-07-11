#include "TagStore.h"

#include <framelift/MediaStoreHelpers.h>
#include <framelift/services/IMediaStore.h>

#include <filesystem>

using framelift::SqlStmt;

namespace
{
constexpr int kSchemaVersion = 2;
}

TagStore::TagStore(IMediaStore* store) : store_(store)
{
}

void TagStore::EnsureSchema()
{
    if (!store_ || store_->GetSchemaVersion("aitagger") == kSchemaVersion)
    {
        return;
    }
    if (store_->GetSchemaVersion("aitagger") != 0)
    {
        (void)store_->Exec("DROP TABLE IF EXISTS aitagger_rule_entries");
        (void)store_->Exec("DROP TABLE IF EXISTS aitagger_rules");
        (void)store_->Exec("DROP TABLE IF EXISTS aitagger_tags");
        (void)store_->Exec("DROP TABLE IF EXISTS aitagger_runs");
        (void)store_->Exec("DROP TABLE IF EXISTS aitagger_files");
    }
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS aitagger_files ("
        "  id    INTEGER PRIMARY KEY,"
        "  path  TEXT    UNIQUE NOT NULL,"
        "  mtime INTEGER NOT NULL DEFAULT 0,"
        "  size  INTEGER NOT NULL DEFAULT 0)"
    );
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS aitagger_runs ("
        "  id            INTEGER PRIMARY KEY,"
        "  file_id       INTEGER NOT NULL,"
        "  model_id      TEXT    NOT NULL,"
        "  rule_id       INTEGER,"
        "  fingerprint   TEXT    NOT NULL,"
        "  started_at    INTEGER NOT NULL DEFAULT 0,"
        "  finished_at   INTEGER NOT NULL DEFAULT 0,"
        "  frames_sampled INTEGER NOT NULL DEFAULT 0,"
        "  status        INTEGER NOT NULL DEFAULT 0)"
    );
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS aitagger_tags ("
        "  id         INTEGER PRIMARY KEY,"
        "  file_id    INTEGER NOT NULL,"
        "  tag        TEXT    NOT NULL,"
        "  confidence REAL    NOT NULL,"
        "  model_id   TEXT    NOT NULL,"
        "  run_id     INTEGER,"
        "  present    INTEGER NOT NULL DEFAULT 0,"
        "  support_count INTEGER NOT NULL DEFAULT 0,"
        "  best_timestamp REAL NOT NULL DEFAULT 0,"
        "  UNIQUE(file_id, tag, model_id))"
    );
    (void)store_->Exec("CREATE INDEX IF NOT EXISTS aitagger_tags_by_file ON aitagger_tags(file_id)");
    (void)store_->Exec("CREATE INDEX IF NOT EXISTS aitagger_tags_by_tag ON aitagger_tags(tag)");
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS aitagger_rules ("
        "  id           INTEGER PRIMARY KEY,"
        "  folder       TEXT    UNIQUE NOT NULL,"
        "  model_id     TEXT    NOT NULL,"
        "  threshold    REAL    NOT NULL DEFAULT 0.6,"
        "  frame_budget INTEGER NOT NULL DEFAULT 31,"
        "  watch        INTEGER NOT NULL DEFAULT 0)"
    );
    (void)store_->Exec(
        "CREATE TABLE IF NOT EXISTS aitagger_rule_entries ("
        "  id        INTEGER PRIMARY KEY,"
        "  rule_id   INTEGER NOT NULL,"
        "  question  TEXT    NOT NULL,"
        "  tag       TEXT    NOT NULL,"
        "  threshold REAL,"
        "  analysis_mode INTEGER NOT NULL DEFAULT 0)"
    );
    (void)store_->Exec(
        "CREATE INDEX IF NOT EXISTS aitagger_rule_entries_by_rule "
        "ON aitagger_rule_entries(rule_id)"
    );
    (void)store_->SetSchemaVersion("aitagger", kSchemaVersion);
}

long long TagStore::UpsertFile(const std::string& path, long long mtime, long long size)
{
    if (!store_)
    {
        return 0;
    }
    SqlStmt s(
        *store_, "INSERT INTO aitagger_files(path, mtime, size) VALUES(?,?,?) "
                 "ON CONFLICT(path) DO UPDATE SET mtime=excluded.mtime, size=excluded.size"
    );
    (void)s.bind(0, path);
    (void)s.bind(1, mtime);
    (void)s.bind(2, size);
    (void)s.step();

    SqlStmt q(*store_, "SELECT id FROM aitagger_files WHERE path=?");
    (void)q.bind(0, path);
    return q.step() == 1 ? q.integer(0) : 0;
}

long long TagStore::BeginRun(
    long long fileId, const std::string& modelId, long long ruleId, const std::string& fingerprint
)
{
    if (!store_)
    {
        return 0;
    }
    SqlStmt s(
        *store_, "INSERT INTO aitagger_runs(file_id, model_id, rule_id, fingerprint, started_at, status) "
                 "VALUES(?,?,?,?,strftime('%s','now'),0)"
    );
    (void)s.bind(0, fileId);
    (void)s.bind(1, modelId);
    if (ruleId > 0)
    {
        (void)s.bind(2, ruleId);
    }
    else
    {
        (void)s.bindNull(2);
    }
    (void)s.bind(3, fingerprint);
    (void)s.step();
    return store_->LastInsertId();
}

void TagStore::FinishRun(long long runId, int framesSampled, int status)
{
    if (!store_ || runId <= 0)
    {
        return;
    }
    SqlStmt s(
        *store_, "UPDATE aitagger_runs SET finished_at=strftime('%s','now'), "
                 "frames_sampled=?, status=? WHERE id=?"
    );
    (void)s.bind(0, static_cast<long long>(framesSampled));
    (void)s.bind(1, static_cast<long long>(status));
    (void)s.bind(2, runId);
    (void)s.step();
}

void TagStore::WriteTags(
    long long fileId, long long runId, const std::string& modelId, const std::vector<aitagger::TagResult>& results
)
{
    if (!store_ || fileId <= 0)
    {
        return;
    }
    (void)store_->Begin();
    {
        SqlStmt del(*store_, "DELETE FROM aitagger_tags WHERE file_id=? AND model_id=?");
        (void)del.bind(0, fileId);
        (void)del.bind(1, modelId);
        (void)del.step();
    }
    SqlStmt ins(
        *store_, "INSERT INTO aitagger_tags(file_id, tag, confidence, model_id, run_id, present, support_count, "
                 "best_timestamp) VALUES(?,?,?,?,?,?,?,?)"
    );
    for (const auto& r : results)
    {
        (void)ins.reset();
        (void)ins.bind(0, fileId);
        (void)ins.bind(1, r.tag);
        (void)ins.bind(2, static_cast<double>(r.confidence));
        (void)ins.bind(3, modelId);
        (void)ins.bind(4, runId);
        (void)ins.bind(5, static_cast<long long>(r.present ? 1 : 0));
        (void)ins.bind(6, static_cast<long long>(r.supportCount));
        (void)ins.bind(7, r.bestTimestamp);
        (void)ins.step();
    }
    (void)store_->Commit();
}

bool TagStore::NeedsTagging(const std::string& path, long long mtime, long long size, const std::string& fingerprint)
{
    if (!store_)
    {
        return true;
    }
    SqlStmt s(
        *store_, "SELECT f.mtime, f.size FROM aitagger_files f WHERE f.path=? AND EXISTS("
                 "SELECT 1 FROM aitagger_runs r WHERE r.file_id=f.id AND r.fingerprint=? AND r.status=0 AND "
                 "r.finished_at>0)"
    );
    (void)s.bind(0, path);
    (void)s.bind(1, fingerprint);
    if (s.step() != 1)
    {
        return true; // never tagged
    }
    return s.integer(0) != mtime || s.integer(1) != size;
}

void TagStore::UpsertRule(aitagger::TagRule& rule)
{
    if (!store_)
    {
        return;
    }
    (void)store_->Begin();
    {
        SqlStmt s(
            *store_, "INSERT INTO aitagger_rules(folder, model_id, threshold, frame_budget, watch) "
                     "VALUES(?,?,?,?,?) ON CONFLICT(folder) DO UPDATE SET "
                     "model_id=excluded.model_id, threshold=excluded.threshold, "
                     "frame_budget=excluded.frame_budget, watch=excluded.watch"
        );
        (void)s.bind(0, rule.folder);
        (void)s.bind(1, rule.modelId);
        (void)s.bind(2, static_cast<double>(rule.threshold));
        (void)s.bind(3, static_cast<long long>(rule.frameBudget));
        (void)s.bind(4, static_cast<long long>(rule.watch ? 1 : 0));
        (void)s.step();
    }
    {
        SqlStmt q(*store_, "SELECT id FROM aitagger_rules WHERE folder=?");
        (void)q.bind(0, rule.folder);
        rule.id = q.step() == 1 ? q.integer(0) : 0;
    }
    {
        SqlStmt del(*store_, "DELETE FROM aitagger_rule_entries WHERE rule_id=?");
        (void)del.bind(0, rule.id);
        (void)del.step();
    }
    SqlStmt ins(
        *store_, "INSERT INTO aitagger_rule_entries(rule_id, question, tag, threshold, analysis_mode) VALUES(?,?,?,?,?)"
    );
    for (const auto& e : rule.entries)
    {
        (void)ins.reset();
        (void)ins.bind(0, rule.id);
        (void)ins.bind(1, e.question);
        (void)ins.bind(2, e.tag);
        if (e.threshold >= 0.0f)
        {
            (void)ins.bind(3, static_cast<double>(e.threshold));
        }
        else
        {
            (void)ins.bindNull(3);
        }
        (void)ins.bind(4, static_cast<long long>(e.analysisMode));
        (void)ins.step();
    }
    (void)store_->Commit();
}

void TagStore::DeleteRule(long long ruleId)
{
    if (!store_ || ruleId <= 0)
    {
        return;
    }
    (void)store_->Begin();
    {
        SqlStmt e(*store_, "DELETE FROM aitagger_rule_entries WHERE rule_id=?");
        (void)e.bind(0, ruleId);
        (void)e.step();
    }
    {
        SqlStmt r(*store_, "DELETE FROM aitagger_rules WHERE id=?");
        (void)r.bind(0, ruleId);
        (void)r.step();
    }
    (void)store_->Commit();
}

void TagStore::LoadRuleEntries(aitagger::TagRule& rule)
{
    if (!store_)
    {
        return;
    }
    SqlStmt s(
        *store_, "SELECT question, tag, threshold, analysis_mode FROM aitagger_rule_entries WHERE rule_id=? ORDER BY id"
    );
    (void)s.bind(0, rule.id);
    while (s.step() == 1)
    {
        aitagger::RuleEntry e;
        e.question = s.str(0);
        e.tag = s.str(1);
        e.threshold = s.isNull(2) ? -1.0f : static_cast<float>(s.num(2));
        e.analysisMode = static_cast<aitagger::AnalysisMode>(s.integer(3));
        rule.entries.push_back(std::move(e));
    }
}

std::vector<aitagger::TagRule> TagStore::ListRules()
{
    std::vector<aitagger::TagRule> out;
    if (!store_)
    {
        return out;
    }
    {
        SqlStmt s(
            *store_, "SELECT id, folder, model_id, threshold, frame_budget, watch FROM aitagger_rules ORDER BY folder"
        );
        while (s.step() == 1)
        {
            aitagger::TagRule r;
            r.id = s.integer(0);
            r.folder = s.str(1);
            r.modelId = s.str(2);
            r.threshold = static_cast<float>(s.num(3));
            r.frameBudget = static_cast<int>(s.integer(4));
            r.watch = s.integer(5) != 0;
            out.push_back(std::move(r));
        }
    }
    for (auto& r : out)
    {
        LoadRuleEntries(r);
    }
    return out;
}

bool TagStore::RuleForFile(const std::string& filePath, aitagger::TagRule& out)
{
    std::error_code ec;
    const std::string dir = std::filesystem::path(filePath).parent_path().string();
    aitagger::TagRule best;
    bool found = false;
    for (auto& r : ListRules())
    {
        // r.folder must equal dir or be a parent directory of it (respecting the '/'
        // boundary so "/videos" doesn't match "/videos2"); the longest match wins.
        const bool matches =
            dir == r.folder || (dir.size() > r.folder.size() && dir.compare(0, r.folder.size(), r.folder) == 0 &&
                                dir[r.folder.size()] == '/');
        if (matches && r.folder.size() >= best.folder.size())
        {
            best = std::move(r);
            found = true;
        }
    }
    if (found)
    {
        out = std::move(best);
    }
    return found;
}

int TagStore::TagCount(const std::string& path)
{
    if (!store_)
    {
        return 0;
    }
    SqlStmt s(
        *store_, "SELECT COUNT(*) FROM aitagger_tags t "
                 "JOIN aitagger_files f ON f.id=t.file_id WHERE f.path=? AND t.present=1"
    );
    (void)s.bind(0, path);
    return s.step() == 1 ? static_cast<int>(s.integer(0)) : 0;
}

bool TagStore::TagAt(const std::string& path, int index, aitagger::TagResult& out)
{
    if (!store_ || index < 0)
    {
        return false;
    }
    SqlStmt s(
        *store_, "SELECT t.tag, t.confidence, t.model_id, t.support_count, t.best_timestamp FROM aitagger_tags t "
                 "JOIN aitagger_files f ON f.id=t.file_id "
                 "WHERE f.path=? AND t.present=1 ORDER BY t.confidence DESC, t.tag LIMIT 1 OFFSET ?"
    );
    (void)s.bind(0, path);
    (void)s.bind(1, static_cast<long long>(index));
    if (s.step() != 1)
    {
        return false;
    }
    out.tag = s.str(0);
    out.confidence = static_cast<float>(s.num(1));
    out.modelId = s.str(2);
    out.supportCount = static_cast<int>(s.integer(3));
    out.bestTimestamp = s.num(4);
    out.present = true;
    return true;
}

bool TagStore::HasTag(const std::string& path, const std::string& tag, float minConfidence)
{
    if (!store_)
    {
        return false;
    }
    SqlStmt s(
        *store_, "SELECT 1 FROM aitagger_tags t JOIN aitagger_files f ON f.id=t.file_id "
                 "WHERE f.path=? AND t.tag=? AND t.confidence>=? LIMIT 1"
    );
    (void)s.bind(0, path);
    (void)s.bind(1, tag);
    (void)s.bind(2, static_cast<double>(minConfidence));
    return s.step() == 1;
}
