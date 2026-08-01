#include <QtTest>
#include <QDirIterator>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

QStringList sourceFilesUnder(const QString& subdir) {
    QStringList files;
    QDirIterator it(u"" UTSUSHI_SOURCE_DIR "/"_s + subdir,
                    {u"*.cpp"_s, u"*.hpp"_s}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    return files;
}

// stripComments() の結果。text はコメントを空白に置換した後のソース、
// endedInsideBlockComment はファイル末尾に達してもなお
// ブロックコメント "/* ... */" が閉じられていなかったことを示す。
struct StripResult {
    QString text;
    bool endedInsideBlockComment = false;
};

// C++ の // 行コメントと /* ... */ ブロックコメントを空白に置換する。
// 文字列リテラル ("...") と文字リテラル ('...') の内部は
// コメント開始記号として解釈しない（バックスラッシュ・エスケープも考慮する）。
//
// 制限事項（意図的に未対応。次に触る人が誤って「対応済み」と思わないよう明記する）:
// - 生文字列リテラル（R"(...)"）は特別扱いしない。R"( ... )" の中身は
//   ふつうの文字列リテラルと誤認識される可能性がある。現状このプロジェクトで
//   生文字列を使っているのは tests/tst_no_hardcode.cpp 自身のみで、その
//   ファイルは stripComments() が適用される唯一の走査（noQt5Api の tests/ 走査）
//   から allowedFileNames で除外済みのため実害はない。core/ や app/ に
//   生文字列リテラルが持ち込まれたら、このコメントごとこの関数を見直すこと。
// - ブロックコメントが閉じずにファイル末尾へ達した場合は、呼び出し側
//   （verifyNoMatch）が endedInsideBlockComment で検出し、その旨をファイル名
//   付きでテスト失敗として報告する。以下で「なぜブロックコメントだけ
//   特別扱いが要るか」を説明する:
//   * ブロックコメント中は文字を空白に置換し続けるため、"*/" に出会わないまま
//     EOF に達すると、それ以降の実コードが丸ごと空白化されて検査から漏れる
//     （= 危険な偽陰性。スキャナが「常に緑」になり得る）。
//   * 行コメント（// ...）が改行なしでファイル末尾まで続くのは、C++ の意味論上
//     正しく「最後の行全体がコメント」であるため、これは未終端バグではない
//     （blankにする対象自体がコメントとして正しい）。特別扱いは不要。
//   * 文字列 "..." / 文字 '...' リテラルが閉じずに EOF へ達した場合でも、
//     このステートマシンはその区間の文字を一切書き換えず、入力をそのまま
//     out に追記し続ける（String/Char の各 case は "\\" エスケープ処理を除き
//     必ず out += c する）。つまり中身が空白化されることはなく、実コードの
//     可視性は失われない（偽陰性を生まない）ため、EOF 到達を特別扱いする
//     必要がない。
// 改行はそのまま残すため、置換後もテキストの行数・全体長は変化しない
// （EOF 到達時に endedInsideBlockComment が立った場合を除き、意味論上の情報は失われない）。
StripResult stripComments(const QString& text) {
    QString out;
    out.reserve(text.size());
    enum class State { Code, LineComment, BlockComment, String, Char };
    State state = State::Code;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        const QChar next = (i + 1 < n) ? text.at(i + 1) : QChar();
        switch (state) {
        case State::Code:
            if (c == u'/' && next == u'/') {
                state = State::LineComment;
                out += u' ';
                out += u' ';
                ++i;
            } else if (c == u'/' && next == u'*') {
                state = State::BlockComment;
                out += u' ';
                out += u' ';
                ++i;
            } else if (c == u'"') {
                state = State::String;
                out += c;
            } else if (c == u'\'') {
                state = State::Char;
                out += c;
            } else {
                out += c;
            }
            break;
        case State::LineComment:
            if (c == u'\n') {
                state = State::Code;
                out += c;
            } else {
                out += u' ';
            }
            break;
        case State::BlockComment:
            if (c == u'*' && next == u'/') {
                state = State::Code;
                out += u' ';
                out += u' ';
                ++i;
            } else if (c == u'\n') {
                out += c;
            } else {
                out += u' ';
            }
            break;
        case State::String:
            if (c == u'\\' && i + 1 < n) {
                out += c;
                out += next;
                ++i;
            } else {
                out += c;
                if (c == u'"') {
                    state = State::Code;
                }
            }
            break;
        case State::Char:
            if (c == u'\\' && i + 1 < n) {
                out += c;
                out += next;
                ++i;
            } else {
                out += c;
                if (c == u'\'') {
                    state = State::Code;
                }
            }
            break;
        }
    }
    return StripResult{out, state == State::BlockComment};
}

// subdir 配下の全ソースを走査し、pattern にマッチしたらテストを落とす。
// コメント本文は事前に stripComments() で空白に置換してから照合するため、
// 説明コメント中に禁止語が出現しても誤検知しない（実コードのみを検査する）。
// allowedFileNames に挙げたファイルだけは走査から除外する。
void verifyNoMatch(const QString& subdir, const QString& pattern,
                   const QStringList& allowedFileNames = {}) {
    const QRegularExpression re(pattern);
    QVERIFY2(re.isValid(), qPrintable(re.errorString()));
    const QStringList files = sourceFilesUnder(subdir);
    for (const QString& path : files) {
        if (allowedFileNames.contains(QFileInfo(path).fileName())) {
            continue;
        }
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        const StripResult stripped = stripComments(QString::fromUtf8(f.readAll()));
        QVERIFY2(!stripped.endedInsideBlockComment,
                 qPrintable(u"%1 はブロックコメント '/* ... */' が閉じられないままファイル末尾に達した"
                            "（未終端のブロックコメント）。これ以降のコードが規約走査から漏れるため、"
                            "内容を確認せずテストを失敗させる。"_s
                                .arg(path)));
        const auto match = re.match(stripped.text);
        QVERIFY2(!match.hasMatch(),
                 qPrintable(u"%1 に禁止パターン '%2' : \"%3\""_s
                                .arg(path, pattern, match.captured())));
    }
}

} // namespace

class TstNoHardcode : public QObject {
    Q_OBJECT
private slots:
    // core/ に QtWidgets・QMessageBox・qDebug が現れない
    void coreHasNoWidgetsOrDebug() {
        verifyNoMatch(u"core"_s,
                      uR"(#include\s*<QtWidgets|QMessageBox|qDebug)"_s);
    }
    // DPI 換算定数 72 は render_size.cpp/.hpp のみ（core/ と app/ を走査）
    void magic72OnlyInRenderSize() {
        const QStringList allowed{u"render_size.cpp"_s, u"render_size.hpp"_s};
        verifyNoMatch(u"core"_s, uR"(\b72\b)"_s, allowed);
        verifyNoMatch(u"app"_s, uR"(\b72\b)"_s, allowed);
    }
    // core/ に生ポインタ new がない（core は unique_ptr のみ。親付き new は app/ 限定）
    void coreHasNoRawNew() {
        verifyNoMatch(u"core"_s, uR"(\bnew\b)"_s);
    }
    // Qt5 API の不在（全域）
    void noQt5Api() {
        const QString pattern =
            uR"(QRegExp|\bSIGNAL\s*\(|\bSLOT\s*\(|\bforeach\s*\(|\bqrand\b|QString::null)"_s;
        verifyNoMatch(u"core"_s, pattern);
        verifyNoMatch(u"app"_s, pattern);
        // このファイル自身はパターン文字列として禁止語を含むため除外する
        // （除外しないと自分のソースにマッチして常に失敗する）
        verifyNoMatch(u"tests"_s, pattern, {u"tst_no_hardcode.cpp"_s});
    }
    // 例外・processEvents の不在（全域）
    void noExceptionsNoProcessEvents() {
        const QString pattern =
            uR"(\bthrow\b|\btry\s*\{|\bcatch\s*\(|processEvents)"_s;
        verifyNoMatch(u"core"_s, pattern);
        verifyNoMatch(u"app"_s, pattern);
    }
};

QTEST_APPLESS_MAIN(TstNoHardcode)
#include "tst_no_hardcode.moc"
