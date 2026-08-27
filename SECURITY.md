# Security policy

TraceGlass is a defensive observability utility. It reads process and network
metadata through documented Windows telemetry APIs and does not inject code,
modify process memory, establish persistence, disable security controls, or
perform remediation.

## Supported versions

Security fixes are provided for the current `0.1.x` release line. Older
development snapshots are not supported.

## Reporting a vulnerability

If the repository has GitHub private vulnerability reporting enabled, use the
**Report a vulnerability** action on the Security tab. Do not publish exploit
details, sensitive host data, or a working proof of concept in a public issue.

For ordinary bugs, crashes, or telemetry correctness problems, open a GitHub
Issue with the Windows version, TraceGlass version, telemetry mode, and minimal
reproduction steps. If a suspected security issue cannot be reported privately,
open a minimally detailed issue asking the maintainer to arrange private
coordination; omit exploit details until that channel exists.

Reports should include the affected version, impact, prerequisites, and the
smallest safe reproduction information available. No maintainer email address
is assumed by this document.
