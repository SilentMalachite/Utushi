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

// C++ の // 行コメントと /* ... */ ブロックコメントを空白に置換する。
// 文字列リテラル ("...") と文字リテラル ('...') の内部は
// コメント開始記号として解釈しない（バックスラッシュ・エスケープも考慮する）。
// 生文字列リテラル（R"(...)"）は特別扱いしない
// （現状の走査対象ソースに出現しないため。将来出現する場合は要見直し）。
// 改行はそのまま残すため、置換後もテキストの行数・全体長は変化しない。
QString stripComments(const QString& text) {
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
    return out;
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
        const QString text = stripComments(QString::fromUtf8(f.readAll()));
        const auto match = re.match(text);
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
