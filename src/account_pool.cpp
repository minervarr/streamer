#include "account_pool.hh"

#include "service_factory.hh"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace account {

namespace {

// How long an auth failure keeps an account at the back of the queue. A
// revoked token can come back to life — the user runs `streamer login` on
// another machine and the same config syncs over — so a demotion has to
// expire, or a one-off failure would blacklist a good account forever.
constexpr int64_t kDemotionSeconds = 6 * 60 * 60;

int64_t now_epoch() { return static_cast<int64_t>(std::time(nullptr)); }

std::string upper(std::string s) {
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool should_failover(Failure f) {
    return f == Failure::Auth || f == Failure::Unavailable;
}

Failure classify(const kb::Error &e) {
    // api_code carries the HTTP status for Http errors and the API's own code
    // for ApiErrorResponse; Qobuz reports a dead token as a 401 in both
    // shapes, which is why this checks the number as well as the enum.
    if (e.api_code == 401 || e.api_code == 403) return Failure::Auth;

    switch (e.code) {
    case kb::ErrorCode::Authentication:
    case kb::ErrorCode::Credentials:
        return Failure::Auth;
    case kb::ErrorCode::ResourceNotFound:
        return Failure::Unavailable;
    case kb::ErrorCode::RateLimit:
    case kb::ErrorCode::Http:
    case kb::ErrorCode::Io:
        return Failure::Network;
    default:
        break;
    }
    if (e.retryable_network) return Failure::Network;
    return Failure::Other;
}

const char *failure_reason(Failure f) {
    switch (f) {
    case Failure::None:        return "";
    case Failure::Auth:        return "auth";
    case Failure::Unavailable: return "unavailable";
    case Failure::Network:     return "network";
    case Failure::Other:       return "other";
    }
    return "";
}

Selector Selector::from_country(const std::string &country) {
    Selector s;
    if (country.empty()) { s.mode = Auto; return s; }
    if (upper(country) == "ALL") { s.mode = All; return s; }
    s.mode = Country;
    s.country = country;
    return s;
}

Pool::Pool(config::Config &cfg) : cfg_(cfg) {}

std::vector<int> Pool::ranked(int64_t now) const {
    // Three tiers, then original file order within each tier (stable_sort).
    // Tier is a small integer rather than a comparator full of conditions so
    // the intent survives being read six months from now.
    auto tier = [&](const config::Account &a) -> int {
        bool failed_last = a.last_fail > a.last_ok;
        bool recent      = failed_last && (now - a.last_fail) < kDemotionSeconds;
        if (recent && a.fail_reason == "auth") return 2;  // known-bad, demoted
        if (a.last_ok > 0 && !failed_last)     return 0;  // known-good
        return 1;                                          // untried, or demotion expired
    };

    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(cfg_.accounts.size()); ++i)
        if (!cfg_.accounts[i].auth_token.empty()) out.push_back(i);

    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return tier(cfg_.accounts[a]) < tier(cfg_.accounts[b]);
    });
    return out;
}

std::vector<int> Pool::ranked() const { return ranked(now_epoch()); }

std::vector<int> Pool::candidates(const Selector &sel) const {
    switch (sel.mode) {
    case Selector::Index:
        if (sel.index >= 0 && sel.index < static_cast<int>(cfg_.accounts.size()))
            return {sel.index};
        return {};

    case Selector::Country: {
        // An explicit --country is a request, not a hard pin: the named
        // account goes first, but the others still back it up, because the
        // whole point is that the user never gets stranded by one dead token.
        std::vector<int> out;
        for (int i = 0; i < static_cast<int>(cfg_.accounts.size()); ++i)
            if (cfg_.accounts[i].country == sel.country && !cfg_.accounts[i].auth_token.empty())
                out.push_back(i);
        for (int i : ranked())
            if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
        return out;
    }

    case Selector::Auto:
    case Selector::All:
    default:
        return ranked();
    }
}

kb::Result<kb::QobuzApiService *> Pool::acquire(int idx) {
    if (idx < 0 || idx >= static_cast<int>(cfg_.accounts.size()))
        return kb::invalid_parameter_error("account index out of range");

    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = services_.find(idx);
        if (it != services_.end()) return &it->second;
    }

    // Built outside the lock: this performs a token login over the network,
    // and holding the mutex across it would serialise every concurrent
    // search and download in the GUI behind one HTTP round-trip.
    auto res = qobuz::make_service(cfg_.accounts[idx]);
    if (!res.ok()) return res.error();

    std::lock_guard<std::mutex> lock(mu_);
    // Another thread may have won the race while we were logging in; keep
    // theirs so every caller shares one service per account.
    auto [it, inserted] = services_.try_emplace(idx, res.take());
    return &it->second;
}

void Pool::reload() {
    std::lock_guard<std::mutex> lock(mu_);
    services_.clear();
    journal_.clear();
}

void Pool::record_ok(int idx) {
    if (idx < 0 || idx >= static_cast<int>(cfg_.accounts.size())) return;
    config::Account &a = cfg_.accounts[idx];
    const int64_t now = now_epoch();
    if (a.last_ok == now && a.fail_reason.empty()) return;  // nothing new to write
    a.last_ok = now;
    a.fail_reason.clear();
    config::persist_account_health(a.country, a.user_id, a.last_ok, a.last_fail, a.fail_reason);
}

void Pool::record_fail(int idx, Failure f, const std::string &message) {
    if (idx < 0 || idx >= static_cast<int>(cfg_.accounts.size())) return;
    config::Account &a = cfg_.accounts[idx];

    {
        std::lock_guard<std::mutex> lock(mu_);
        journal_.push_back(Attempt{idx, a.country, f, message});
    }

    // Only auth failures are the account's own fault. A network outage or a
    // regional gap says nothing about this account's health, and recording it
    // would demote a perfectly good account for being asked the wrong thing.
    if (f != Failure::Auth) return;

    a.last_fail   = now_epoch();
    a.fail_reason = failure_reason(f);
    // A dead token means the cached service is dead too — drop it so a later
    // login (fixed out of band) is picked up without restarting.
    {
        std::lock_guard<std::mutex> lock(mu_);
        services_.erase(idx);
    }
    config::persist_account_health(a.country, a.user_id, a.last_ok, a.last_fail, a.fail_reason);
}

void Pool::clear_journal() {
    std::lock_guard<std::mutex> lock(mu_);
    journal_.clear();
}

std::string Pool::report() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (journal_.empty()) return {};

    std::string out;
    for (const Attempt &at : journal_) {
        const std::string who = at.country.empty() ? "(no country)" : at.country;
        out += who + ": " + at.message + "\n";
        if (at.failure == Failure::Auth) {
            out += "  -> token expired or revoked; repair with:  streamer login --country "
                 + who + "\n";
        }
    }
    return out;
}

} // namespace account
