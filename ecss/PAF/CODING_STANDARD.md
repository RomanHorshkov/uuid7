# Coding standard and description of tools {#UUID7_DOC_CODING_STANDARD}

<!-- ecss:docinfo id=UUID7-DOC-CODSTD -->

The conventions every repository of the kyr platform follows, stated for uuid7 (ECSS-Q-ST-80 6.3.4.1: coding standards including security, naming and commentary rules; 6.3.3.2: design standards). They are not aspirations: each rule names what enforces it, a gate of the pipeline when one exists, else the inspection that checks it (\ref tc_UUID7_IN_008 "UUID7-IN-008"). A rule with a gate cannot be broken on `master`: the pipeline that guards it runs locally and in CI on every push.

## 1 Format

| Rule | Statement | Enforced by |
|---|---|---|
| F1 | The source is formatted by clang-format 18 with the platform's `.clang-format`: Google base, 4-space indent, no tabs, 140 columns, Allman braces, no space between a keyword and its parenthesis (`if(x)`), pointer bound to the type (`void* p`), consecutive assignments, declarations and macros aligned, preprocessor directives indented after the hash, includes sorted within their block. | pipeline stage `format` (`utils/format.sh --check`, clang-format major 18, the major CI pins) |
| F2 | `.clang-format` is kyr-build's, byte for byte, ending in a newline. | pipeline stage `template` |
| F3 | A file is grouped in banner sections in this order: public defines, public structured variables, public function typedefs, public function declarations (header); private defines, private structured variables, private variables, private function prototypes, private inline functions, public function definitions, private function definitions, sanity checks (source). An empty section says `/* None */`. | inspection |
| F4 | Comments that a reflow would break carry `NOCFMT`. | clang-format (`CommentPragmas`) |

## 2 Naming

| Rule | Statement | Enforced by |
|---|---|---|
| N1 | Every external symbol starts with the library's prefix (`uuid7_`), every public macro and constant with its upper case (`UUID7_`); one lowercase prefix per foundation library equal to its name (NAMING.md). | ABI gate (`KYR_ABI_*_PREFIX`, stage `abi`); \ref tc_UUID7_AN_007 "UUID7-AN-007" |
| N2 | Types end in `_t`; function pointer types too (`uuid7_rng_function_t`). | ABI gate (types are read from the header); inspection |
| N3 | Functions are verbs on their subject (`uuid7_gen`, `uuid7_raise_floor`, `uuid7_set_rng_func`). | inspection |
| N4 | Private (static) functions start with `_`, file-scope variables with `g_`, private macros with the module's short upper prefix (`V7_`). | inspection |
| N5 | The header guard is the prefix and the file (`UUID7_H`). | inspection |
| N6 | Test-only symbols are `<prefix>_test_*`, declared in `<name>_test.h`, compiled only with `-D<PREFIX>_TESTING`. | linker map (`uuid7_test_*` exported only from test builds); \ref tc_UUID7_AN_004 "UUID7-AN-004" |

## 3 Documentation

| Rule | Statement | Enforced by |
|---|---|---|
| D1 | Every file opens with a doc comment: `@file`, `@brief`, the design notes a reader needs, `@author Roman Horshkov <https://github.com/RomanHorshkov>` and `@date`. | inspection; doxygen |
| D2 | Every public symbol carries a doc comment: brief, every parameter with its direction, every return value. | \ref tc_UUID7_AN_010 "UUID7-AN-010" (doxygen, `WARN_IF_UNDOCUMENTED`, `WARN_NO_PARAMDOC`) |
| D3 | Every documented symbol of `app/` names its design component (`@ingroup uuid7_cmp_*`) and the requirements it implements (`@satisfies UUID7-SRS-nnn`); every test names what it verifies (`@verifies`). | `ecss/make.sh`: the generated matrices show any requirement without code or test |
| D4 | Implementation comments state why, not what; a line the code already says gets no comment. READMEs are terse and never hard-wrapped. | inspection |

## 4 Errors

| Rule | Statement | Enforced by |
|---|---|---|
| E1 | A function that can fail returns `int`: 0 on success, a distinct negative code per cause, each documented with `@return`. | inspection; the ICD's tests per return value |
| E2 | A library never reports through `errno`, never calls `abort()`, `assert()` or `exit()`, never raises a signal. | inspection (\ref tc_UUID7_IN_004 "UUID7-IN-004") |
| E3 | On failure, outputs are left untouched and shared state unchanged: validate, then compute, then commit with one atomic step. | tests (\ref tc_UUID7_TC_IT_rng_failure_consumes_no_sequence "UUID7-TC-IT-rng_failure_consumes_no_sequence", the abuse-bound tests) |
| E4 | Arguments a caller can get wrong are checked (`NULL` → -1); internal invariants are compile-time (`_Static_assert`). | tests; inspection |
| E5 | A failure that cannot be returned (a clock read inside generation) is counted and exposed, never silent. | inspection (\ref tc_UUID7_IN_002 "UUID7-IN-002") |

## 5 Memory and resources

| Rule | Statement | Enforced by |
|---|---|---|
| M1 | No hidden allocation: a library allocates nothing; the caller owns every buffer and passes its size or a documented fixed size. | \ref tc_UUID7_AN_001 "UUID7-AN-001" (`nm` of the release library) |
| M2 | State is static, fixed and initialised at load; no constructor, no teardown. | \ref tc_UUID7_AN_003 "UUID7-AN-003" |
| M3 | No VLA, no `alloca`. | compiler: `-Wvla -Walloca` in every strict profile |
| M4 | A descriptor is opened with `O_CLOEXEC` and closed on every path. | inspection (\ref tc_UUID7_IN_001 "UUID7-IN-001") |

## 6 Concurrency

| Rule | Statement | Enforced by |
|---|---|---|
| C1 | Shared state is C11 `_Atomic` with an explicit memory order on every access, and a comment saying why that order suffices. | inspection; the sanitize cells; ThreadSanitizer where the repository runs it |
| C2 | No lock and no wait on a hot path; a retry loop retries only after a failed compare-and-swap. | inspection (\ref tc_UUID7_AN_012 "UUID7-AN-012") |

## 7 Build, warnings and hardening

| Rule | Statement | Enforced by |
|---|---|---|
| B1 | C11 (`-std=c11`), `_POSIX_C_SOURCE=200809L`; `_GNU_SOURCE` only in the file that needs it. | the profile catalog of the pinned kyr-build |
| B2 | Warning-free under every profile of the catalog (release below; audit adds `-fanalyzer` and more). | \ref tc_UUID7_AN_005 "UUID7-AN-005" |
| B3 | The code passes the sanitize profile (ASan, UBSan, LSan) in every suite. | pipeline stage `integration` (sanitize cells) |
| B4 | Release artifacts are hardened: stack protector, stack clash protection, CET, `_FORTIFY_SOURCE=3`, PIC, full RELRO, immediate binding, non-executable stack, `-z defs`, versioned soname, no runpath, allowlisted dependencies. | `check_hardening.sh` (stages `build:release` and `package`; \ref tc_UUID7_TC_PL_hardening "UUID7-TC-PL-hardening") |
| B5 | `utils/` holds only kyr-build's wrappers (byte for byte), the project file (declarations only) and the linker map; no copied build machinery. | pipeline stage `template` |

The release profile's flags, as the pinned catalog prints them:

<!-- ecss:flags profile=release -->

## 8 Interface and versions

| Rule | Statement | Enforced by |
|---|---|---|
| A1 | The public ABI is committed (`abi/layout.txt`): a changed or removed line needs a new major, a new name a new minor. | pipeline stage `abi` |
| A2 | The shared object exports exactly the declared functions, as `<name>@@<NODE>_<major>`; the soname major is the SemVer major. | pipeline stage `abi-exports`; linker version script `utils/uuid7.map` |
| A3 | Release tags are `v<VERSION>`, annotated and signed, on a commit whose pipeline is green. | release workflow (cicd `c-release`) |

## 9 Dependencies, tiers and reproducibility

| Rule | Statement | Enforced by |
|---|---|---|
| P1 | Everything a build needs that the repository does not contain is a line of `deps.txt`: `apt` (minimum version), `vendor`, or `own` (exact version and SHA-256 of the release deb) (DEPENDENCIES.md). | `utils/deps.sh` (refuses a hash mismatch, removes the file) |
| P2 | The repository declares its tier (`# tier: 1 foundation`) and pins only strictly lower tiers. | pipeline stage `deps-tiers` |
| P3 | A commit builds the same bytes anywhere: `SOURCE_DATE_EPOCH` from the commit, `-ffile-prefix-map`, deterministic packaging; releases in the pinned build root, whose identity the `.buildinfo` records. | \ref tc_UUID7_AN_008 "UUID7-AN-008" |

## 10 Tests

| Rule | Statement | Enforced by |
|---|---|---|
| T1 | Every requirement has a verification case; every test names the requirements it verifies. | `ecss/make.sh` (the gaps of the \ref UUID7_DOC_TRACE "matrices") |
| T2 | The suites run against every profile's archive and shared object; the coverage run measures them; test seams exist only in the test builds. | pipeline stage `integration` (`KYR_IT_*`, `KYR_TEST_HOOKS_*`) |
| T3 | Each test resets the library first and asserts every outcome it relies on; no sleep, no dependence on test order. | inspection |
| T4 | The installed packages are tested as installed (headers and libraries from the extracted debs only). | pipeline stage `package-smoke` |

## 11 Tools

| Tool | Use | Version |
|---|---|---|
| gcc | compiler, `-fanalyzer` | the build root's (13.3) or the host's |
| clang-format | F1 | 18 (CI's) |
| kyr-build | profiles, wrappers, gates, packaging | pinned in deps.txt |
| CMocka, gcovr | tests, coverage | distribution's (deps.txt minimum) |
| doxygen, graphviz, PlantUML | documentation, diagrams | distribution's; PlantUML MIT jar |
| kyr-rms-cli | requirements management | the platform's |
