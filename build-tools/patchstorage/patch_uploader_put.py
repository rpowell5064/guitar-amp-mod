#!/usr/bin/env python3
"""Work around PatchStorage's hosting edge blocking HTTP PUT (2026-09-27).

Their host (Interneto vizija) began returning an HTML 403 error page for PUT,
PATCH and DELETE on https://patchstorage.com/api/beta/... *before* the request
reaches WordPress. POST still passes through and answers with normal JSON.

The uploader we clone (patchstorage/patchstorage-lv2-uploader) updates an
existing listing with `requests.put(PS_API_URL + '/patches/<id>')`, so every
update died at the edge and the client reported "Failed to decode JSON response"
— it had been handed HTML. Files themselves kept uploading fine, because those
go by POST, which is why v1.20.0 shipped new media against stale listings.

The WordPress REST API honours a method override, so the fix is to send the same
body by POST with `X-HTTP-Method-Override: PUT`. Verified against their API
unauthenticated: the override returns a proper JSON
`{"code":"rest_cannot_edit","status":401}` where a bare PUT returns the host's
HTML 403 — i.e. the override reaches the edit endpoint and a bare PUT does not.
This is a documented mechanism of their own API, not a bypass of an application
control.

Method probe, /api/beta/patches/194103:
    POST                  -> 404 rest_no_route        (JSON, reaches the app)
    PUT / PATCH / DELETE  -> 403 text/html            (host error page)
    POST + override       -> 401 rest_cannot_edit     (JSON, reaches the app)

REMOVE THIS once PatchStorage allow PUT again: delete the call in
.github/workflows/release.yml and this file. The uploader itself is unmodified
upstream (unchanged since 2024-05), so nothing here is a fork to maintain.

Usage: python build-tools/patchstorage/patch_uploader_put.py <path to uploader.py>
Exits non-zero if the call is not in the expected shape, so a silent no-op can
never masquerade as a successful patch.
"""
import io
import sys

OLD = (
    "        resp = requests.put(PS_API_URL + '/patches/' + str(pid), json=data, headers={\n"
    "            'Authorization': 'Bearer ' + Patchstorage.PS_API_TOKEN,\n"
    "            'User-Agent': Patchstorage.USER_AGENT\n"
    "        })"
)

NEW = (
    "        resp = requests.post(PS_API_URL + '/patches/' + str(pid), json=data, headers={\n"
    "            'Authorization': 'Bearer ' + Patchstorage.PS_API_TOKEN,\n"
    "            'User-Agent': Patchstorage.USER_AGENT,\n"
    "            'X-HTTP-Method-Override': 'PUT'\n"
    "        })"
)


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: patch_uploader_put.py <uploader.py>\n")
        return 2
    path = sys.argv[1]
    src = io.open(path, encoding="utf-8").read()

    if "'X-HTTP-Method-Override'" in src:
        print("patch_uploader_put: already patched, nothing to do")
        return 0
    if OLD not in src:
        sys.stderr.write(
            "patch_uploader_put: the update call in %s is not in the expected shape.\n"
            "Refusing to patch blindly — inspect it and update OLD/NEW here.\n" % path)
        return 1

    io.open(path, "w", encoding="utf-8", newline="\n").write(src.replace(OLD, NEW, 1))
    print("patch_uploader_put: update call -> POST + X-HTTP-Method-Override: PUT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
