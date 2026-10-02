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

시작 시간과 100MB 기본 메모리 목표를 반복 측정하는 opt-in 도구와 Windows/macOS
실행 명령은 [성능 예산 측정 문서](docs/PERFORMANCE_BUDGET.md)를 참고하세요.

패키지 기본값도 명시적으로 고정되어 있습니다. Windows 패키지는 upstream
Scintilla를 Release 구성으로 빌드하고 `Scintilla.dll`과
`Scintilla-LICENSE.txt`를 `axyne.exe` 옆에 설치합니다. macOS 패키지는
기본적으로 `arm64` 아키텍처와 macOS 12.0 이상을 대상으로 하며, 생성되는
패키지 파일 이름에 아키텍처가 포함됩니다. 필요한 경우 다음 CMake 옵션으로
macOS 값을 바꿀 수 있습니다.

패키징은 Release 구성만 지원합니다. 멀티 구성 생성기에서는 `cpack -C Release`를
사용해야 하며, Debug 패키징 요청은 거부됩니다.

```sh
cmake -S . -B build \
  -DCMAKE_OSX_ARCHITECTURES="arm64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="12.0"
```
