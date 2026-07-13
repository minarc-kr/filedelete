#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
완전삭제 (안전하게 파일 지우기)
============================================
고른 파일을 무작위 데이터로 여러 번 덮어쓴 뒤 지워서
일반 복구 프로그램으로는 되살릴 수 없게 만드는 프로그램입니다.

- Windows / macOS / Linux 어디서나 됨
- 어린이도 쓸 수 있게 아주 쉽게 만들었습니다.
    * 그냥 실행하면 큰 버튼이 있는 창이 뜹니다.
    * ① [파일 고르기] 를 누르고  ② [삭제하기] 를 누르면 끝!

(어른/전문가용) 터미널에서 이렇게도 쓸 수 있습니다:
    python secure_delete.py "파일경로"
    python secure_delete.py a.txt b.jpg --passes 7 --yes

⚠️  안내
    - 지운 파일은 되돌릴 수 없습니다. 꼭 확인하세요.
    - SSD, USB, SD카드 같은 저장장치는 기술적 특성상 완전삭제가
      100% 보장되지 않을 수 있습니다.
"""

import os
import sys
import argparse

DEFAULT_PASSES = 7  # 덮어쓰기 기본 횟수


# ----------------------------------------------------------------------
# 핵심 로직: 파일 한 개를 안전하게 덮어쓰고 삭제
# ----------------------------------------------------------------------
def secure_delete_file(path, passes=DEFAULT_PASSES, progress=None):
    """path 를 passes 번 덮어쓴 뒤 삭제. 실패 시 예외 발생."""
    if not os.path.exists(path):
        raise FileNotFoundError(f"파일이 없습니다: {path}")
    if os.path.islink(path):
        os.unlink(path)  # 링크는 원본을 건드리지 않고 링크만 제거
        return True
    if not os.path.isfile(path):
        raise ValueError(f"파일이 아닙니다: {path}")

    length = os.path.getsize(path)

    try:
        os.chmod(path, 0o600)  # 읽기전용 파일 대비
    except Exception:
        pass

    chunk_size = 1024 * 1024  # 1MB
    with open(path, "r+b", buffering=0) as f:
        for p in range(passes):
            f.seek(0)
            remaining = length
            while remaining > 0:
                n = min(chunk_size, remaining)
                f.write(os.urandom(n))
                remaining -= n
            f.flush()
            os.fsync(f.fileno())  # 실제 디스크에 기록
            if progress:
                progress(f"덮어쓰기 {p + 1}/{passes}", (p + 1) / (passes + 1))
        # 크기 정보도 지우기
        f.seek(0)
        f.truncate(0)
        f.flush()
        os.fsync(f.fileno())

    # 파일 이름 흔적도 줄이기 위해 무작위 이름으로 변경 후 삭제
    folder = os.path.dirname(os.path.abspath(path))
    current = path
    for _ in range(3):
        rand_name = os.path.join(folder, "".join("%02x" % b for b in os.urandom(12)))
        try:
            os.rename(current, rand_name)
            current = rand_name
        except Exception:
            break
    os.remove(current)
    if progress:
        progress("삭제 완료", 1.0)
    return True


def collect_files(paths):
    """폴더가 들어오면 안의 모든 파일을 펼쳐서 반환."""
    files = []
    for p in paths:
        if os.path.isdir(p) and not os.path.islink(p):
            for root, _dirs, names in os.walk(p):
                for name in names:
                    files.append(os.path.join(root, name))
        else:
            files.append(p)
    return files


def remove_dir_tree(folder):
    """파일을 모두 지운 뒤 남은 빈 하위 폴더와 폴더 자체를 아래에서
    위로 제거한다. 제거한 폴더 개수를 반환한다."""
    removed = 0
    if not os.path.isdir(folder) or os.path.islink(folder):
        return 0
    for root, dirs, names in os.walk(folder, topdown=False):
        for name in names:
            fp = os.path.join(root, name)
            try:
                os.chmod(fp, 0o600)
            except Exception:
                pass
            try:
                os.remove(fp)
            except Exception:
                pass
        for d in dirs:
            dp = os.path.join(root, d)
            try:
                if os.path.islink(dp):
                    os.unlink(dp)
                else:
                    os.rmdir(dp)
                removed += 1
            except Exception:
                pass
    try:
        os.rmdir(folder)
        removed += 1
    except Exception:
        pass
    return removed


# ----------------------------------------------------------------------
# 명령줄(CLI) 모드 — 어른/전문가용
# ----------------------------------------------------------------------
def run_cli(paths, passes, assume_yes):
    # '폴더 통째로' 대상: 삭제 후 폴더 자체도 제거
    selected_folders = [p for p in paths
                        if os.path.isdir(p) and not os.path.islink(p)]
    files = collect_files(paths)
    if not files and not selected_folders:
        print("삭제할 파일이 없습니다.")
        return 1

    print("=" * 60)
    print("다음 파일을 완전삭제합니다 (복구 불가):")
    for fpath in files:
        try:
            print(f"  - {fpath}  ({os.path.getsize(fpath):,} bytes)")
        except OSError:
            print(f"  - {fpath}")
    print(f"덮어쓰기 횟수: {passes}회")
    print("=" * 60)

    if not assume_yes:
        answer = input("정말 삭제할까요? 되돌릴 수 없습니다. (yes 입력): ").strip().lower()
        if answer not in ("yes", "y", "예", "네"):
            print("취소했습니다.")
            return 0

    ok, fail = 0, 0
    for fpath in files:
        try:
            secure_delete_file(fpath, passes)
            print(f"  [OK] 삭제 완료: {fpath}")
            ok += 1
        except Exception as e:
            print(f"  [X ] 실패: {fpath}  -> {e}")
            fail += 1

    # 선택한 폴더의 남은 빈 폴더 및 폴더 자체 제거
    folders_removed = 0
    for folder in selected_folders:
        folders_removed += remove_dir_tree(folder)

    print("-" * 60)
    print(f"완료: 성공 {ok}건, 실패 {fail}건, 제거한 폴더 {folders_removed}개")
    return 0 if fail == 0 else 2


# ----------------------------------------------------------------------
# 창(GUI) 모드 — 어린이도 쓸 수 있게 아주 단순하게
# ----------------------------------------------------------------------
def run_gui():
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox
    except Exception:
        print("이 환경에서는 창 모드를 쓸 수 없습니다. 터미널에서 파일 경로를 지정해 실행하세요.")
        print('예)  python secure_delete.py "파일경로"')
        return 1

    selected = []          # 삭제할 파일 목록
    selected_folders = []  # '폴더 통째로'로 고른 폴더 (삭제 후 폴더 자체도 제거)

    BG = "#f4f7fb"
    root = tk.Tk()
    root.title("안전하게 파일 지우기")
    root.geometry("640x600")
    root.minsize(560, 560)
    root.configure(bg=BG)

    # ----- 제목 -----
    tk.Label(root, text="🧹 안전하게 파일 지우기",
             font=("Arial", 22, "bold"), bg=BG, fg="#1a3d5c").pack(pady=(18, 4))
    tk.Label(root, text="한 번 지우면 되살릴 수 없어요. 순서대로 눌러 보세요!",
             font=("Arial", 12), bg=BG, fg="#4a6070").pack(pady=(0, 12))

    # ----- ① 파일 고르기 -----
    step1 = tk.Label(root, text="① 지울 파일 고르기",
                     font=("Arial", 14, "bold"), bg=BG, fg="#2c3e50")
    step1.pack(anchor="w", padx=24)

    pick_row = tk.Frame(root, bg=BG)
    pick_row.pack(pady=(4, 6))

    def refresh():
        listbox.delete(0, tk.END)
        for f in selected:
            listbox.insert(tk.END, "  📄  " + os.path.basename(f))
        count_label.config(text=f"고른 파일: {len(selected)}개")

    def add_files():
        paths = filedialog.askopenfilenames(title="지울 파일 고르기")
        for p in paths:
            if p and p not in selected:
                selected.append(p)
        refresh()

    def add_folder():
        folder = filedialog.askdirectory(title="폴더 고르기 (안의 파일 전부 지움)")
        if not folder:
            return
        for f in collect_files([folder]):
            if f not in selected:
                selected.append(f)
        if folder not in selected_folders:
            selected_folders.append(folder)  # 폴더 자체도 삭제하도록 기록
        refresh()

    def clear_all():
        selected.clear()
        selected_folders.clear()
        refresh()

    tk.Button(pick_row, text="📁 파일 고르기", font=("Arial", 13, "bold"),
              bg="#3498db", fg="white", activebackground="#2e86c1",
              width=14, height=2, relief="flat", command=add_files
              ).grid(row=0, column=0, padx=6)
    tk.Button(pick_row, text="🗂 폴더 통째로", font=("Arial", 13, "bold"),
              bg="#5dade2", fg="white", activebackground="#3498db",
              width=14, height=2, relief="flat", command=add_folder
              ).grid(row=0, column=1, padx=6)
    tk.Button(pick_row, text="🧽 목록 비우기", font=("Arial", 13),
              bg="#bdc3c7", fg="#2c3e50", activebackground="#95a5a6",
              width=12, height=2, relief="flat", command=clear_all
              ).grid(row=0, column=2, padx=6)

    # ----- 고른 파일 목록 -----
    frame = tk.Frame(root, bg=BG)
    frame.pack(fill="both", expand=True, padx=24, pady=(2, 4))
    scrollbar = tk.Scrollbar(frame)
    scrollbar.pack(side="right", fill="y")
    listbox = tk.Listbox(frame, yscrollcommand=scrollbar.set,
                         font=("Arial", 12), activestyle="none",
                         bg="white", relief="solid", bd=1, highlightthickness=0)
    listbox.pack(side="left", fill="both", expand=True)
    scrollbar.config(command=listbox.yview)

    count_label = tk.Label(root, text="고른 파일: 0개", font=("Arial", 11),
                           bg=BG, fg="#4a6070")
    count_label.pack()

    # ----- ② 삭제하기 -----
    tk.Label(root, text="② 지우기",
             font=("Arial", 14, "bold"), bg=BG, fg="#2c3e50"
             ).pack(anchor="w", padx=24, pady=(6, 2))

    status_label = tk.Label(root, text="", font=("Arial", 11, "bold"),
                            bg=BG, fg="#c0392b")
    status_label.pack()

    def do_delete():
        if not selected and not selected_folders:
            messagebox.showinfo("잠깐!", "먼저 지울 파일이나 폴더를 골라 주세요. 😊")
            return

        if selected_folders:
            msg = (f"고른 파일 {len(selected)}개를 완전히 지우고,\n"
                   f"선택한 폴더 {len(selected_folders)}개(하위 폴더 포함)도 제거합니다.\n\n"
                   "한 번 지우면 다시 살릴 수 없어요.\n정말 지울까요?")
        else:
            msg = (f"고른 파일 {len(selected)}개를 완전히 지웁니다.\n\n"
                   "한 번 지우면 다시 살릴 수 없어요.\n정말 지울까요?")
        if not messagebox.askyesno("정말 지울까요?", msg, icon="warning"):
            return

        files = list(selected)
        total = len(files)
        ok, fail, errors = 0, 0, []
        for i, fpath in enumerate(files):
            status_label.config(text=f"지우는 중...  ({i + 1}/{total})")
            root.update_idletasks()
            try:
                secure_delete_file(fpath, DEFAULT_PASSES)
                ok += 1
                selected.remove(fpath)
            except Exception as e:
                fail += 1
                errors.append(f"{os.path.basename(fpath)} -> {e}")
            root.update_idletasks()

        # 선택한 폴더의 남은 빈 폴더 및 폴더 자체 제거
        folders_removed = 0
        if selected_folders:
            status_label.config(text="폴더 정리 중...")
            root.update_idletasks()
            for folder in list(selected_folders):
                folders_removed += remove_dir_tree(folder)
            selected_folders.clear()

        refresh()
        status_label.config(text="")
        if fail == 0:
            if folders_removed > 0:
                messagebox.showinfo(
                    "완료!",
                    f"파일 {ok}개와 폴더 {folders_removed}개를 안전하게 지웠어요! 🎉")
            else:
                messagebox.showinfo("완료!", f"파일 {ok}개를 안전하게 지웠어요! 🎉")
        else:
            detail = "\n".join(errors[:8])
            messagebox.showwarning(
                "일부 실패",
                f"지운 파일: {ok}개\n못 지운 파일: {fail}개\n"
                f"제거한 폴더: {folders_removed}개\n\n{detail}")

    tk.Button(root, text="🗑  지우기", font=("Arial", 18, "bold"),
              bg="#e74c3c", fg="white", activebackground="#c0392b",
              height=2, relief="flat", command=do_delete
              ).pack(fill="x", padx=24, pady=(4, 6))

    tk.Label(root,
             text="※ USB·SD카드·SSD는 완전삭제가 100% 보장되지 않을 수 있어요.",
             font=("Arial", 9), bg=BG, fg="#95a5a6").pack(pady=(0, 10))

    refresh()
    root.mainloop()
    return 0


# ----------------------------------------------------------------------
# 진입점
# ----------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="고른 파일을 여러 번 덮어쓴 뒤 완전 삭제합니다.")
    parser.add_argument("paths", nargs="*", help="삭제할 파일 또는 폴더 경로")
    parser.add_argument("--passes", type=int, default=DEFAULT_PASSES,
                        help=f"덮어쓰기 횟수 (기본 {DEFAULT_PASSES})")
    parser.add_argument("--yes", action="store_true", help="확인 없이 바로 삭제")
    parser.add_argument("--gui", action="store_true", help="무조건 창 모드로 실행")
    args = parser.parse_args()

    if args.gui or not args.paths:
        return run_gui()
    return run_cli(args.paths, max(1, args.passes), args.yes)


if __name__ == "__main__":
    sys.exit(main())
