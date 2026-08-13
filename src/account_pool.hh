#pragma once

// The one place that decides *which* account talks to Qobuz, and what to do
// when it can't. Both the CLI and the GUI go through here, so "FR's token is
// dead, use NZ instead" behaves identically in each — a policy duplicated in
// two front-ends is a policy that drifts.
//
// Two facts drive the design:
//
//   * Availability is regional. The same album can be present in one account's
//     country and absent in another's, so "not found" is a reason to ask a
//     different account, not a reason to give up.
//   * A stored auth_token expires or gets revoked silently. Before this
//     existed, that turned into a bare `API error 401` from whichever account
//     happened to sit first in config.toml, and every subcommand failed even
//     though a perfectly good account sat one slot below.

#include "config.hh"

#include <api/service.hh>
#include <core/errors.hh>

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace account {

// Why an attempt failed, reduced to the only question that matters here:
// would asking a *different* account help?
enum class Failure {
    None,
    Auth,         // expired/revoked token, bad credentials — try another account
    Unavailable,  // not offered in this account's region      — try another account
    Network,      // transport down, or rate-limited           — do NOT try another
    Other,        // parse errors, bad parameters              — do NOT try another
};

// Network never fails over: if the link is down, walking five accounts takes
// five times as long and then reports "no account worked", which is a lie.
// Rate limiting classifies as Network for the same reason — the limit is per
// app_id and every account shares one, so switching cannot help.
bool should_failover(Failure f);

Failure classify(const kb::Error &e);

// Human-readable, and specific enough to act on ("token expirado" vs "401").
const char *failure_reason(Failure f);

// Which account(s) a command wants. Auto is the common case: whichever is
// healthiest, then whichever is next, until one answers.
struct Selector {
    enum Mode { Auto, Country, Index, All } mode = Auto;
    std::string country;   // Mode::Country
    int         index = -1; // Mode::Index — the GUI's --account N

    // "" -> Auto, "all" (any case) -> All, anything else -> that country.
    static Selector from_country(const std::string &country);
};

// One entry per account tried, in order. This is what turns the old bare 401
// into a report the user can act on.
struct Attempt {
    int         account_idx = -1;
    std::string country;
    Failure     failure = Failure::None;
    std::string message;
};

class Pool {
public:
    // Borrows `cfg`; it must outlive the pool. Health writes go both to this
    // in-memory copy and, through config::persist_account_health, to disk.
    explicit Pool(config::Config &cfg);

    // Account indices to try, in order, for `sel`. For Auto/All this is
    // ranked(): accounts that worked recently first, ones that failed auth
    // last. Never reorders cfg.accounts itself.
    std::vector<int> candidates(const Selector &sel) const;

    // Health-ranked view of every usable account (has a token). Pure given
    // the config and `now`; `now` is a parameter rather than a call to
    // time(): it makes the ranking testable without waiting for the clock.
    std::vector<int> ranked(int64_t now) const;
    std::vector<int> ranked() const;

    // Builds (or returns cached) an authenticated service for account `idx`.
    // The returned pointer stays valid for the pool's lifetime.
    kb::Result<kb::QobuzApiService *> acquire(int idx);

    // Drops every cached service. Call after the borrowed Config changes on
    // disk (the GUI's Settings save), because a cached service still carries
    // the credentials it logged in with — without this, repairing a token in
    // Settings would appear to do nothing until the app restarted.
    void reload();

    void record_ok(int idx);
    void record_fail(int idx, Failure f, const std::string &message);

    const std::vector<Attempt> &journal() const { return journal_; }
    void clear_journal();

    // A ready-to-print account of what was tried and what to do about it —
    // the whole point of the exercise. Empty when nothing failed.
    std::string report() const;

    const config::Config &config() const { return cfg_; }

    // Runs `fn` against each candidate until one succeeds. Returns that
    // result, or — if every candidate failed — the last error, with the
    // journal explaining the whole walk.
    //
    // Deliberately NOT used for Selector::All: that mode wants every
    // account's answer merged, not the first one that works. Callers doing
    // that (multi-country search) drive candidates() + acquire() themselves.
    template <class T>
    kb::Result<T> with_service(
        const Selector &sel,
        const std::function<kb::Result<T>(kb::QobuzApiService &, const config::Account &)> &fn) {
        clear_journal();
        std::vector<int> cands = candidates(sel);
        if (cands.empty())
            return kb::credentials_error("no account is configured with an auth token");

        std::optional<kb::Error> last;
        for (int idx : cands) {
            auto svc = acquire(idx);
            if (!svc.ok()) {
                Failure f = classify(svc.error());
                record_fail(idx, f, svc.error().message);
                last = svc.error();
                if (!should_failover(f)) break;
                continue;
            }
            auto res = fn(*svc.value(), cfg_.accounts[idx]);
            if (res.ok()) {
                record_ok(idx);
                return res;
            }
            Failure f = classify(res.error());
            record_fail(idx, f, res.error().message);
            last = res.error();
            if (!should_failover(f)) break;
        }
        return last ? *last : kb::credentials_error("no account could serve the request");
    }

private:
    config::Config &cfg_;
    // std::map, not vector: acquire() hands out pointers that must stay valid
    // as later accounts are added to the cache.
    std::map<int, kb::QobuzApiService> services_;
    std::vector<Attempt> journal_;
    mutable std::mutex mu_;
};

} // namespace account
