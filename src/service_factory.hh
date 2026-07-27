#pragma once

// One place to build a kb::QobuzApiService from a stored account. Everything
// that talks to Qobuz — the CLI, the GUI's search, settings and download
// paths — goes through here so they all get the same credential-refresh
// wiring: when Qobuz rejects a rotated app_secret, the service re-scrapes the
// web player and this factory's listener writes the new pair back to
// config.toml.

#include "config.hh"

#include <api/service.hh>

// Namespaced `qobuz` rather than `svc` because `svc` is the conventional
// local variable name for a service throughout this codebase.
namespace qobuz {

// Builds a service for `account` with the credential-refresh listener
// installed. When `authenticate` is true (the default) and the account has a
// stored token, also performs the token login; a login failure is returned as
// an error.
kb::Result<kb::QobuzApiService> make_service(const config::Account &account,
                                             bool authenticate = true);

} // namespace qobuz
