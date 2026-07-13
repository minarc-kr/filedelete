## v1.0.0

Windows용 파일 완전삭제 유틸리티 첫 릴리즈.

**기능**
- 고른 파일을 무작위 데이터로 7회 덮어쓴 뒤 삭제
- 파일 다중 선택 · 폴더 통째로 선택
- 설치 불필요, 단일 실행파일 (Portable)
- 인터넷 통신 · 개인정보 수집 · 백그라운드 상주 없음

**요구사항**
- Windows 10 / 11 (64비트), Windows 11 ARM 호환

**무결성 검증** — `secure_delete.exe`
```
크기      186,464 bytes
SHA-256   5b0b3107c252e706a2a1cd566115fbbf56a0c50169b6331ea9b96017a379e172
```
```powershell
Get-FileHash .\secure_delete.exe -Algorithm SHA256
```

**참고**
- 코드 서명 인증서를 사용하지 않아 첫 실행 시 SmartScreen 경고가 표시될 수 있습니다.
- SSD·USB 등 플래시 저장장치에서는 완전삭제가 보장되지 않습니다. README의 "한계"를 확인하세요.
