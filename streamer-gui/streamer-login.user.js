// ==UserScript==
// @name         Streamer – Copy login credentials
// @namespace    streamer-qobuz
// @version      2.0
// @description  Intercepts Qobuz API requests to capture auth token and user ID, then copies the streamer login command to clipboard
// @match        https://play.qobuz.com/*
// @grant        none
// @run-at       document-start
// ==/UserScript==

(function () {
    'use strict';

    let capturedToken  = null;
    let capturedUserId = null;

    // ── Intercept fetch ───────────────────────────────────────────────────────

    const origFetch = window.fetch;
    window.fetch = function (...args) {
        const req = args[0];
        const init = args[1] || {};

        // Pull token from request headers
        try {
            let headers = init.headers || (req instanceof Request ? req.headers : null);
            if (headers) {
                const get = (h, k) => (h instanceof Headers ? h.get(k) : (h[k] ?? h[k.toLowerCase()]));
                const t = get(headers, 'X-User-Auth-Token');
                if (t) capturedToken = t;
            }
        } catch (_) {}

        // Also sniff the response JSON for user_auth_token / user.id
        return origFetch.apply(this, args).then(resp => {
            if (capturedToken && capturedUserId) return resp;
            const url = typeof req === 'string' ? req : req?.url ?? '';
            if (!url.includes('qobuz.com')) return resp;
            const clone = resp.clone();
            clone.json().then(data => {
                if (data?.user_auth_token && !capturedToken)  capturedToken  = data.user_auth_token;
                if (data?.user?.id        && !capturedUserId) capturedUserId = String(data.user.id);
                if (data?.user_auth_token && !capturedUserId && data?.user?.id)
                    capturedUserId = String(data.user.id);
                updateBtn();
            }).catch(() => {});
            return resp;
        });
    };

    // ── Intercept XHR ────────────────────────────────────────────────────────

    const origOpen = XMLHttpRequest.prototype.open;
    const origSetHeader = XMLHttpRequest.prototype.setRequestHeader;

    XMLHttpRequest.prototype.setRequestHeader = function (name, value) {
        if (name === 'X-User-Auth-Token' && value) capturedToken = value;
        return origSetHeader.apply(this, arguments);
    };

    XMLHttpRequest.prototype.open = function (method, url, ...rest) {
        this.addEventListener('load', function () {
            if (capturedToken && capturedUserId) return;
            if (!url.includes('qobuz.com')) return;
            try {
                const data = JSON.parse(this.responseText);
                if (data?.user_auth_token && !capturedToken)  capturedToken  = data.user_auth_token;
                if (data?.user?.id        && !capturedUserId) capturedUserId = String(data.user.id);
                updateBtn();
            } catch (_) {}
        });
        return origOpen.apply(this, [method, url, ...rest]);
    };

    // ── UI ────────────────────────────────────────────────────────────────────

    function createBtn() {
        const btn = document.createElement('button');
        btn.id = 'streamer-copy-btn';
        btn.textContent = '⏳ Waiting for login…';
        Object.assign(btn.style, {
            position:     'fixed',
            bottom:       '20px',
            right:        '20px',
            zIndex:       '999999',
            padding:      '10px 16px',
            background:   '#1a1a2e',
            color:        '#e0e0ff',
            border:       '1px solid #5555aa',
            borderRadius: '8px',
            fontSize:     '13px',
            cursor:       'default',
            boxShadow:    '0 2px 8px rgba(0,0,0,0.5)',
            fontFamily:   'monospace',
            userSelect:   'none',
            opacity:      '0.6',
        });

        btn.addEventListener('click', () => {
            if (!capturedToken || !capturedUserId) return;
            const command = `streamer login --user-id "${capturedUserId}" --token "${capturedToken}"`;
            navigator.clipboard.writeText(command).then(() => {
                btn.textContent = '✓ Copied!';
                btn.style.background = '#1a3a1a';
                setTimeout(() => {
                    btn.textContent = '📋 Copy streamer login';
                    btn.style.background = '#1a1a2e';
                }, 2000);
            }).catch(() => {
                prompt('[streamer] Copy this command:', command);
            });
        });

        document.body.appendChild(btn);
        return btn;
    }

    function updateBtn() {
        const btn = document.getElementById('streamer-copy-btn');
        if (!btn) return;
        if (capturedToken && capturedUserId) {
            btn.textContent = '📋 Copy streamer login';
            btn.style.opacity = '1';
            btn.style.cursor  = 'pointer';
            btn.title = `User ID: ${capturedUserId}`;
        }
    }

    // Create button once DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', createBtn);
    } else {
        createBtn();
    }
})();
