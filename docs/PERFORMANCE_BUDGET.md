# 성능 예산 측정

`axyne_perf_benchmark`는 Axyne의 시작 시간과 기본 시작 상태의 메모리를 반복
측정하는 opt-in 도구입니다. 정상 `axyne` 실행 파일에는 이 도구나 추가 계측이
링크되지 않으며, 앱 시작 시 외부 프로세스나 런타임을 시작하지 않습니다.

## 빌드

도구는 기존 CMake 빌드에 `axyne_perf_benchmark` target으로 포함됩니다. 기존
Scintilla configure/build 요구사항은 그대로 적용되며, 측정 도구가 외부 런타임을
설치하거나 다운로드하지는 않습니다.

macOS:

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-perf --config Release --target axyne_perf_benchmark axyne
```

Windows PowerShell:

```powershell
cmake -S . -B build-perf -A x64 -DBUILD_TESTING=ON
cmake --build build-perf --config Release --target axyne_perf_benchmark axyne
```

## Headless 회귀 기준

인자 없이 실행하면 `axyne_core`의 `axyne_app_initialize`/shutdown 경계를 같은
프로세스에서 반복합니다. 이는 앱 UI가 아니라 공유 코어의 시작 비용을 빠르게
회귀 검사하는 모드입니다.

```sh
./build-perf/tools/perf/benchmarks/axyne_perf_benchmark \
  --headless --iterations 10 --warmup 1 --json
```

Windows에서는 다음 실행 파일 경로를 사용합니다.

```powershell
.\build-perf\tools\perf\benchmarks\Release\axyne_perf_benchmark.exe `
  --headless --iterations 10 --warmup 1 --json
```

CTest에는 외부 프로세스를 시작하지 않는 기본 회귀 케이스가 등록됩니다.

```sh
ctest --test-dir build-perf -R axyne-performance-budget --output-on-failure
```

이 케이스는 측정 3회, warmup 1회, 시작 시간 1,000ms, 메모리 100MB 기준을
사용합니다. 장시간 CI 안정성을 위해 이 기준은 제품의 OS 실측 결과를 대신하지
않고, 도구 연결·출력·예산 판정이 계속 작동하는지 검증합니다.

## 실제 앱 측정

`--target`은 지정한 Axyne 실행 파일만 명시적으로 시작하고, 각 반복이 끝나면
종료합니다. 따라서 아래 명령은 개발자가 직접 실행할 때만 프로세스를 만들며,
정상 앱 실행에는 영향을 주지 않습니다.

macOS 앱 번들 내부 실행 파일:

```sh
./build-perf/tools/perf/benchmarks/axyne_perf_benchmark \
  --target "$PWD/build-perf/Axyne.app/Contents/MacOS/Axyne" \
  --iterations 10 --warmup 2 --settle-ms 500 \
  --startup-budget-ms 3000 --memory-budget-mb 100 --json
```

Windows PowerShell:

```powershell
.\build-perf\tools\perf\benchmarks\Release\axyne_perf_benchmark.exe `
  --target (Resolve-Path .\build-perf\Release\axyne.exe) `
  --iterations 10 --warmup 2 --settle-ms 500 `
  --startup-budget-ms 3000 --memory-budget-mb 100 --json
```

실제 앱 측정 모드는 플랫폼 기본 API만 사용합니다.

- Windows 시작 시간은 프로세스 생성부터 `WaitForInputIdle`까지이며, 메모리는
  대상 프로세스의 working set입니다.
- macOS 시작 시간은 프로세스 생성부터 대상 프로세스의 `proc_pidinfo` 메모리
  정보가 처음 관찰될 때까지이며, 메모리는 settle window 동안 관찰한 최대 RSS입니다.
- `--settle-ms`는 초기 UI/런타임 로딩을 관찰할 시간입니다. 0이면 시작 관찰 직후
  한 번만 샘플합니다.
- 측정 결과가 `--startup-budget-ms` 또는 `--memory-budget-mb`를 넘으면 exit code
  2를 반환합니다. 실행 실패나 샘플링 실패는 exit code 1입니다.

JSON 출력의 `startup_ms.max`와 `peak_resident_mb`를 OS별 기준선에 기록하고,
저메모리 개선 전후에는 동일한 빌드 구성·반복 횟수·settle window·실행 환경으로
비교해야 합니다. 정확한 Windows/macOS 실측값은 해당 OS 호스트에서 이 명령을
직접 실행한 결과로만 확정합니다.

도움말은 다음으로 확인할 수 있습니다.

```sh
axyne_perf_benchmark --help
```
