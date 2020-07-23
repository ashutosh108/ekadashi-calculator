#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "fmt-format-fixed.h"
#include <iterator>
#include <QDate>
#include <QMessageBox>

#include "location.h"
#include "text-interface.h"
#include "tz-fixed.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QString::fromStdString(vp::text_ui::program_name_and_version()));
    setupLocationsComboBox();
    setDateToToday();
    showVersionInStatusLine();
}

MainWindow::~MainWindow()
{
    delete ui;
}

date::sys_days to_sys_days(QDate qd)
{
    using namespace date;
    return sys_days{days{qd.toJulianDay()} -
        (sys_days{1970_y/January/1} - sys_days{year{-4713}/November/24})};
}

// parse tiny subset of markdown in a single plain text line, return HTML version
QString htmlify_line(const std::string_view & line) {
    QString res = QString::fromUtf8(line.data(), line.size()).toHtmlEscaped();

    if (res.startsWith("# ")) {
        return "<h1>" + res + "</h1>";
    }

    res.replace(QRegularExpression{R"~((with Atiriktā (?:Ekādaśī|Dvādaśī)))~"}, "<span style=\"color:red\">\\1</span>");

    // Highlight the second half or "on YYYY-MM-DD & YYYY-MM-DD" with red
    // Note: when changing this regex, be careful to not confuse "(" and ")"
    // of raw string literal syntax and of regexp itself.
    res.replace(QRegularExpression{R"~((on \d\d\d\d-\d\d-\d\d )(\S{1,10} \d\d\d\d-\d\d-\d\d))~"}, "\\1<span style=\"color:red\">\\2</span>");

    int pos = 0;
    while(1) {
        auto start = res.indexOf("**", pos);
        if (start == -1) break;
        auto end = res.indexOf("**", start+2);
        if (end == -1) break;
        // 9 is 2 (length of "**") + 3 (length of "<b>") + 4 (length of "</b>")
        pos = end + 9;
        res = res.replace(start, 2, "<b>**");
        // end+3 because "<b>**" in previous replacement is three chars longer than "**"
        res = res.replace(end+3, 2, "**</b>");
    }
    return res;
}

static QString get_html_from_detail_view(const std::string_view & s) {
    QString res;
    for (auto first = s.data(), second = s.data(), last = first + s.size(); second != last && first != last; first = second + 1) {
        second = std::find(first, last, '\n');

        if (first != second) {
            res += htmlify_line(std::string_view{first, static_cast<std::string::size_type>(second - first)});
            res += "<br>\n";
        }
    }

    return res;
}

void MainWindow::on_FindNextEkadashi_clicked()
{
    try {
        date::year_month_day date = to_sys_days(ui->dateEdit->date());

        auto location_string = ui->locationComboBox->currentText();

        fmt::memory_buffer buf;
        if (location_string == "all") {
            calcAll(date, buf);
        } else {
            calcOne(date, location_string, buf);
        }

        QString detail_html = get_html_from_detail_view(std::string_view{buf.data(), buf.size()});
        ui->calcResult->setHtml(detail_html);
    } catch (std::exception &e) {
        QMessageBox::warning(this, "error", e.what());
    } catch (...) {
        QMessageBox::warning(this, "internal error", "unexpected exception thrown");
    }
}

void MainWindow::setupLocationsComboBox()
{
    ui->locationComboBox->addItem("all");
    for (const auto &l : vp::text_ui::LocationDb()) {
        ui->locationComboBox->addItem(l.name);
    }
}

void MainWindow::setDateToToday()
{
    ui->dateEdit->setDate(QDate::currentDate());
}

void MainWindow::calcAll(date::year_month_day base_date, fmt::memory_buffer & buf)
{
    for (auto &l : vp::text_ui::LocationDb()) {
        calcOne(base_date, l.name, buf);
    }
}

void MainWindow::calcOne(date::year_month_day base_date, QString location_string, fmt::memory_buffer & buf)
{
    QByteArray location_as_bytearray = location_string.toLocal8Bit();
    char * location_name = location_as_bytearray.data();

    auto vrata = vp::text_ui::find_calc_and_report_one(base_date, location_name, buf);
    if (vrata.has_value()) {
        ui->locationName->setText(QString::fromStdString(vrata->location_name()));

        ui->vrataType->setText(QString::fromStdString(fmt::to_string(vrata->type)));

        if (vp::is_atirikta(vrata->type)) {
            auto next_day = date::year_month_day{date::sys_days{vrata->date} + date::days{1}};
            ui->vrataDate->setText(QString::fromStdString(fmt::format("{} and {}", vrata->date, next_day)));
        } else {
            ui->vrataDate->setText(QString::fromStdString(fmt::to_string(vrata->date)));
        }

        ui->paranamNextDay->setText(QString::fromStdString(fmt::format("Pāraṇam <span style=\" font-size:small;\">({})</span>:", vrata->local_paran_date())));

        std::string paranTime = vp::ParanFormatter::format(
                    vrata->paran,
                    vrata->location.timezone_name,
                    "%H:%M<span style=\"font-size:small;\">:%S</span>",
                    "–",
                    "%H:%M<span style=\"font-size:small;\">:%S</span>",
                    "<sup>*</sup><br><small><sup>*</sup>");
        paranTime += "</small>";
        ui->paranTime->setText(QString::fromStdString(paranTime));
    }
}

void MainWindow::showVersionInStatusLine()
{
    statusBar()->showMessage(QString::fromStdString(vp::text_ui::program_name_and_version()));
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, "About", QString::fromStdString(vp::text_ui::program_name_and_version())
                       + "<br><br>Download new versions: <a href=\"https://github.com/ashutosh108/vaishnavam-panchangam/releases\">https://github.com/ashutosh108/vaishnavam-panchangam/releases</a>"
                       "<br><br>Support this program: <a href=\"https://www.patreon.com/vaishnavam_panchangam\">https://www.patreon.com/vaishnavam_panchangam</a>");
}

void MainWindow::on_actionE_xit_2_triggered()
{
    QApplication::quit();
}

void MainWindow::clearLocationData() {
    ui->latitude->setText("(multiple)");
    ui->longitude->setText("(multiple)");
    ui->timezone->setText("(multiple values)");
}

void MainWindow::on_locationComboBox_currentIndexChanged(const QString &location_name)
{
    if (location_name == "all") return clearLocationData();
    auto location_arr = location_name.toUtf8();
    auto location = vp::text_ui::LocationDb().find_coord(location_arr.data());
    if (!location.has_value()) return;
    ui->latitude->setText(QString::fromStdString(fmt::to_string(location->latitude)));
    ui->longitude->setText(QString::fromStdString(fmt::to_string(location->longitude)));
    ui->timezone->setText(location->timezone_name);
}
