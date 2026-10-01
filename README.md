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

Windows에서는 Win32 창과 공식 Scintilla Win32 컨트롤을, macOS에서는 AppKit 창과
공식 Scintilla Cocoa framework를 함께 빌드합니다. 최초 configure 시 공식
Scintilla 저장소의 고정 revision을 내려받기 위해 네트워크 연결이 필요합니다.
Windows는 Visual Studio C++ Build Tools(MSBuild), macOS는 Xcode command-line
tools(`xcodebuild`)가 필요합니다. Scintilla 원본과 라이선스는 공식
[저장소](https://github.com/mirror/scintilla)와 [라이선스](https://github.com/mirror/scintilla/blob/rel-5-5-2/License.txt)를
참고하세요. 앱 출력물에는 Scintilla 바이너리와 라이선스 고지가 함께 포함됩니다.

제품 메타데이터는 CMake 옵션으로 조정할 수 있습니다.

```sh
cmake -S . -B build \
  -DAXYNE_DISPLAY_NAME="Axyne" \
  -DAXYNE_AUTHOR="Native" \
  -DAXYNE_VERSION="0.1.0"
```
