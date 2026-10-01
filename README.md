<p align="center">
  <img src="./default.png" alt="Axyne logo" width=100>
</p>

<h1 align="center">Axyne</h1>

<p align="center">경량화된 IDE 데스크탑 프로그램</a>

## 개발 시작

현재 저장소에는 C17 기반 공통 코어와 플랫폼별 애플리케이션 진입점의
최소 구조가 들어 있습니다.

```sh
cmake -S . -B build
cmake --build build
```

Windows에서는 Win32 창을, macOS에서는 AppKit 창을 빌드합니다.

제품 메타데이터는 CMake 옵션으로 조정할 수 있습니다.

```sh
cmake -S . -B build \
  -DAXYNE_DISPLAY_NAME="Axyne" \
  -DAXYNE_AUTHOR="Native" \
  -DAXYNE_VERSION="0.1.0"
```
