#!/bin/bash
#
# 내PC클리너 (My PC Cleaner) — macOS 버전  v1.1.0
# =====================================================================
# 고른 파일/폴더를 무작위 데이터로 7회 덮어쓴 뒤 삭제하여, 일반 복구
# 프로그램으로는 되살릴 수 없게 만듭니다.
#
# macOS에 기본 내장된 도구(osascript · dd · shell)만 사용합니다.
# 파이썬 등 어떤 설치도 필요 없습니다.
#
# 인터넷 통신 · 개인정보 수집 없이, 오직 사용자가 고른 대상만 삭제합니다.
# =====================================================================

PASSES=7
export LANG=ko_KR.UTF-8

# --- 대화상자 도우미 ---
dialog() { osascript -e "$1" 2>/dev/null; }

# --- 파일 한 개 안전삭제 ---
secure_delete_file() {
    local f="$1"
    [ -f "$f" ] || return 1
    chmod u+w "$f" 2>/dev/null
    # 파일 크기 (macOS: stat -f%z / 리눅스: stat -c%s — 양쪽 호환)
    local size
    size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null || echo 0)
    local i
    for ((i=0; i<PASSES; i++)); do
        if [ "$size" -gt 0 ]; then
            local blocks=$(( (size + 1048575) / 1048576 ))
            dd if=/dev/urandom of="$f" bs=1048576 count="$blocks" conv=notrunc 2>/dev/null
        fi
        sync
    done
    : > "$f"                      # 크기 0으로
    # 이름 흔적 줄이기: 무작위 이름으로 변경 후 삭제
    local dir rnd
    dir=$(dirname "$f")
    rnd="$dir/$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n')"
    mv "$f" "$rnd" 2>/dev/null && f="$rnd"
    rm -f "$f"
    [ ! -e "$f" ]
}

# --- 실행 ---
main() {
    # 무엇을 지울지 선택
    local mode
    mode=$(dialog 'set m to choose from list {"파일 고르기", "폴더 통째로"} with title "내PC클리너" with prompt "무엇을 완전삭제할까요?" default items {"파일 고르기"} without empty selection allowed
if m is false then
return "CANCEL"
else
return item 1 of m
end if')
    [ -z "$mode" ] || [ "$mode" = "CANCEL" ] && exit 0

    local -a files=()
    local -a folders=()

    if [ "$mode" = "파일 고르기" ]; then
        local list
        list=$(dialog 'set out to ""
set theFiles to choose file with prompt "지울 파일을 고르세요 (Command 또는 Shift로 여러 개 선택)" with multiple selections allowed
repeat with f in theFiles
set out to out & (POSIX path of f) & linefeed
end repeat
return out')
        [ -z "$list" ] && exit 0
        while IFS= read -r line; do
            [ -n "$line" ] && files+=("$line")
        done <<< "$list"
    else
        local folder
        folder=$(dialog 'POSIX path of (choose folder with prompt "폴더를 고르세요 (안의 모든 파일이 삭제됩니다)")')
        [ -z "$folder" ] && exit 0
        folder="${folder%/}"
        folders+=("$folder")
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find "$folder" -type f -print0 2>/dev/null)
    fi

    local nfiles=${#files[@]}
    local nfolders=${#folders[@]}
    if [ "$nfiles" -eq 0 ] && [ "$nfolders" -eq 0 ]; then
        dialog 'display dialog "지울 파일이 없습니다." buttons {"확인"} default button "확인" with title "내PC클리너"'
        exit 0
    fi

    # 최종 확인
    local confirmMsg
    if [ "$nfolders" -gt 0 ]; then
        confirmMsg="고른 폴더 안의 파일 ${nfiles}개를 완전히 지우고, 선택한 폴더(하위 폴더 포함)도 제거합니다.\n\n한 번 지우면 되살릴 수 없어요. 정말 지울까요?"
    else
        confirmMsg="고른 파일 ${nfiles}개를 완전히 지웁니다.\n\n한 번 지우면 되살릴 수 없어요. 정말 지울까요?"
    fi
    local answer
    answer=$(dialog "display dialog \"$confirmMsg\" buttons {\"취소\", \"지우기\"} default button \"취소\" with icon caution with title \"내PC클리너\"
return button returned of result")
    [ "$answer" != "지우기" ] && exit 0

    # 삭제 수행
    local ok=0 fail=0
    for f in "${files[@]}"; do
        if secure_delete_file "$f"; then ok=$((ok+1)); else fail=$((fail+1)); fi
    done

    # 폴더의 빈 하위 폴더 및 폴더 자체 제거 (아래에서 위로)
    local removed=0
    for folder in "${folders[@]}"; do
        while IFS= read -r -d '' d; do
            if rmdir "$d" 2>/dev/null; then removed=$((removed+1)); fi
        done < <(find "$folder" -depth -type d -print0 2>/dev/null)
    done

    # 결과 안내
    local resultMsg
    if [ "$fail" -eq 0 ]; then
        if [ "$removed" -gt 0 ]; then
            resultMsg="파일 ${ok}개와 폴더 ${removed}개를 안전하게 지웠어요! 🎉"
        else
            resultMsg="파일 ${ok}개를 안전하게 지웠어요! 🎉"
        fi
    else
        resultMsg="지운 파일: ${ok}개, 못 지운 파일: ${fail}개, 제거한 폴더: ${removed}개\n(사용 중이거나 권한이 없는 파일이 있을 수 있어요.)"
    fi
    dialog "display dialog \"$resultMsg\" buttons {\"확인\"} default button \"확인\" with title \"내PC클리너\""
}

main
