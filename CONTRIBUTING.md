# Contributing

Contributions are welcome. Please note the licensing model before contributing.

## Licensing of contributions (important)

This project is **dual-licensed** (GPL-3.0-or-later + a commercial license — see
[`LICENSING.md`](LICENSING.md)). To keep offering the commercial license, the
project must hold the rights to all contributed code.

**By submitting a contribution (pull request, patch, etc.), you agree that:**

1. You are the author of the contribution and have the right to submit it; and
2. You grant Ryan Powell a perpetual, irrevocable, worldwide license to use,
   modify, and **relicense** your contribution, including under both the GPL and
   the commercial license — i.e. you allow it to be dual-licensed alongside the
   rest of the project.

This is a lightweight inbound=outbound + relicensing grant (similar to a CLA). If
you can't agree to that, please open an issue to discuss before sending code.

Sign off commits to indicate agreement:

```
git commit -s   # adds "Signed-off-by: Your Name <you@example.com>"
```

## Practical notes

- DSP changes: research the real circuit and verify offline against captures
  before deploying — see the project notes; don't guess-and-spiral.
- Keep model/effect display names trademark-clean (parody names).
- Never commit third-party captures (.nam/.ir/.wav) — only your own.
