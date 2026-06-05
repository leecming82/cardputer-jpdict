#!/usr/bin/env python3
import unicodedata


DISPLAY_CHAR_MAP = {
    "\u5167": "\u5185",  # 內 -> 内
    "\u654e": "\u6559",  # 敎 -> 教
    "\u7522": "\u7523",  # 產 -> 産
    "\u7a05": "\u7a0e",  # 稅 -> 税
    "\u8aaa": "\u8aac",  # 說 -> 説
    "\u9751": "\u9752",  # 靑 -> 青
    "\u543f": "\u544a",  # 吿 -> 告
    "\u7d55": "\u7d76",  # 絕 -> 絶
    "\u6236": "\u6238",  # 戶 -> 戸
    "\u812b": "\u8131",  # 脫 -> 脱
    "\u6df8": "\u6e05",  # 淸 -> 清
    "\u6b72": "\u6b73",  # 歲 -> 歳
    "\u95b1": "\u95b2",  # 閱 -> 閲
    "\u92b3": "\u92ed",  # 銳 -> 鋭
    "\u5c19": "\u5c1a",  # 尙 -> 尚
    "\u5433": "\u5449",  # 吳 -> 呉
    "\u6085": "\u60a6",  # 悅 -> 悦
    "\u5a1b": "\u5a2f",  # 娛 -> 娯
    "\u5f65": "\u5f66",  # 彥 -> 彦
    "\u902c": "\u8ff8",  # 逬 -> 迸
    "\u663b": "\u6602",  # 昻 -> 昂
    "\u7575": "\u753b",  # 畵 -> 画
    "\u9ad9": "\u9ad8",  # 髙 -> 高
    "\uff5e": "~",
    "\u27f6": "->",
    "\u2122": "TM",
}


def normalize_display_text(text):
    normalized = unicodedata.normalize("NFKC", text)
    return "".join(DISPLAY_CHAR_MAP.get(ch, ch) for ch in normalized)


def dedupe_chars(text):
    seen = set()
    out = []
    for ch in text:
        if ch in seen:
            continue
        seen.add(ch)
        out.append(ch)
    return "".join(out)
