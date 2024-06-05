/*
 * Copyright (c) 2020-2022 Alex Spataru <https://github.com/alex-spataru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <QDir>
#include <QUrl>                  //相关库和头文件
#include <QFileInfo>
#include <QApplication>
#include <QDesktopServices>        //为文件和目录操作、应用程序管理和桌面服务提供支持

#include <AppInfo.h>               //应用信息
#include <IO/Manager.h>            //输入输出管理
#include <CSV/Export.h>            //CSV导出
#include <JSON/Editor.h>           //JSON编辑
#include <UI/Dashboard.h>          //用户界面
#include <Misc/Utilities.h>        //辅助功能
#include <Misc/TimerEvents.h>      //时间相关的事件处理
//项目自定义的头文件
/**
 * Connect JSON Parser & Serial Manager signals to begin registering JSON
 * dataframes into JSON list.
 */
CSV::Export::Export()
    : m_fieldCount(0)              //m_fieldCount 被设置为 0，表明开始时没有字段计数
    , m_exportEnabled(true)        //m_exportEnabled 被设置为 true，意味着默认情况下允许数据导出
{
    auto io = &IO::Manager::instance();
    auto te = &Misc::TimerEvents::instance();       //两行代码通过调用 instance() 方法获取 IO::Manager 和 Misc::TimerEvents 类的单例实例。这两个单例通常用于管理整个应用程序或模块的输入/输出和定时事件。
    connect(io, &IO::Manager::connectedChanged, this, &Export::closeFile);
    connect(io, &IO::Manager::frameReceived, this, &Export::registerFrame);
    connect(te, &Misc::TimerEvents::timeout1Hz, this, &Export::writeValues);
}

/**
 * Close file & finnish write-operations before destroying the class
 */
CSV::Export::~Export()
{
    closeFile();
}

/**
 * Returns a pointer to the only instance of this class
 */
CSV::Export &CSV::Export::instance()
{
    static Export singleton;
    return singleton;
}

/**
 * Returns @c true if the CSV output file is open
 */
bool CSV::Export::isOpen() const    //const 修饰符表示该函数不会修改类的任何成员变量（不会改变对象的状态），因此可以在任何 const CSV::Export 对象上调用它。
{
    return m_csvFile.isOpen();
}

/**
 * Returns @c true if CSV export is enabled
 */
bool CSV::Export::exportEnabled() const
{
    return m_exportEnabled;
}

/**
 * Open the current CSV file in the Explorer/Finder window
 */
void CSV::Export::openCurrentCsv()
{
    if (isOpen())
        Misc::Utilities::revealFile(m_csvFile.fileName());
    else
        Misc::Utilities::showMessageBox(tr("CSV file not open"),
                                        tr("Cannot find CSV export file!"));
}

/**
 * Enables or disables data export
 */
void CSV::Export::setExportEnabled(const bool enabled)
{
    m_exportEnabled = enabled;
    Q_EMIT enabledChanged();

    if (!exportEnabled() && isOpen())
    {
        m_frames.clear();
        closeFile();
    }
}

/**
 * Write all remaining JSON frames & close the CSV file
 */
void CSV::Export::closeFile()
{
    if (isOpen())
    {
        while (!m_frames.isEmpty())
            writeValues();

        m_fieldCount = 0;
        m_csvFile.close();
        m_textStream.setDevice(Q_NULLPTR);

        Q_EMIT openChanged();
    }
}

/**
 * Creates a CSV file based on the JSON frames contained in the JSON list.
 * @note This function is called periodically every 1 second.
 */
void CSV::Export::writeValues()
{
    // Get separator sequence
    auto sep = IO::Manager::instance().separatorSequence();

    // Write each frame
    for (auto i = 0; i < m_frames.count(); ++i)
    {
        auto frame = m_frames.at(i);
        auto fields = QString::fromUtf8(frame.data).split(sep);

        // File not open, create it & add cell titles
        if (!isOpen() && exportEnabled())
            createCsvFile(frame);

         // Write RX date/time
        m_textStream << frame.rxDateTime.toString("yyyy/MM/dd/ HH:mm:ss::zzz") << ",";

        // Write frame data
        for (auto j = 0; j < fields.count(); ++j)
        {
            m_textStream << fields.at(j);
            if (j < fields.count() - 1)
                m_textStream << ",";

            else
            {
                auto d = m_fieldCount - fields.count();
                if (d > 0)
                {
                    for (auto k = 0; k < d - 1; ++k)
                        m_textStream << ",";
                }

                m_textStream << "\n";
            }
        }
    }

    // Clear frames
    m_frames.clear();
}

/**
 * Creates a new CSV file corresponding to the current project title & field count
 */
void CSV::Export::createCsvFile(const CSV::RawFrame &frame)
{ // Get project title
    auto projectTitle = UI::Dashboard::instance().title();

    // Get file name
    const QString fileName = frame.rxDateTime.toString("HH-mm-ss") + ".csv";

    // Get path
    // clang-format off
    const QString format = frame.rxDateTime.toString("yyyy/MMM/dd/");
    const QString path = QString("%1/Documents/%2/CSV/%3/%4").arg(QDir::homePath(),
                                                                  qApp->applicationName(),
                                                                  projectTitle, format);
    // clang-format on

    // Generate file path if required
    QDir dir(path);
    if (!dir.exists())
        dir.mkpath(".");

    // Open file
    m_csvFile.setFileName(dir.filePath(fileName));
    if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        Misc::Utilities::showMessageBox(tr("CSV File Error"),
                                        tr("Cannot open CSV file for writing!"));
        closeFile();
        return;
    }

    // Add cell titles & force UTF-8 codec
    m_textStream.setDevice(&m_csvFile);
    m_textStream.setGenerateByteOrderMark(true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_textStream.setCodec("UTF-8");
#else
    m_textStream.setEncoding(QStringConverter::Utf8);
#endif
    // Get number of fields by counting datasets with non-duplicated indexes
    QVector<int> fields;
    QVector<QString> titles;
    for (int i = 0; i < JSON::Editor::instance().groupCount(); ++i)
    {
        for (int j = 0; j < JSON::Editor::instance().datasetCount(i); ++j)
        {
            auto dataset = JSON::Editor::instance().getDataset(i, j);
            if (!fields.contains(dataset.index()))
            {
                fields.append(dataset.index());
                titles.append(dataset.title());
            }
        }
    }
    // Add table titles
    m_fieldCount = fields.count();
    m_textStream << "RX Date/Time,";
    for (auto i = 0; i < m_fieldCount; ++i)
    {
        m_textStream << titles.at(i) << "(field " << i + 1 << ")";

        if (i < m_fieldCount - 1)
            m_textStream << ",";
        else
            m_textStream << "\n";
    }

    // Update UI
    Q_EMIT openChanged();
}

/**
 * Appends the latest data from the device to the output buffer
 */
void CSV::Export::registerFrame(const QByteArray &data)
{
    // Ignore if device is not connected (we don't want to generate a CSV file when we
    // are reading another CSV file don't we?)
    if (!IO::Manager::instance().connected())   //通过调用IO::Manager类的单例实例检查设备是否连接。

        return;//如果设备未连接，函数直接返回，不注册Frame

    // Ignore if current dashboard frame hasn't been loaded yet
    if (!UI::Dashboard::instance().currentFrame().isValid())  //通过调用UI::Dashboard类的单例实例检查当前帧是否有效
        return;

    // Ignore if CSV export is disabled
    if (!exportEnabled())  //检查数据导出功能是否被启用
        return;

    // Register raw frame to list
    RawFrame frame;
    frame.data = data;
    frame.rxDateTime = QDateTime::currentDateTime();   //获取当前的日期和时间，并赋值给frame的rxDateTime成员变量
    m_frames.append(frame);   //将新的frame对象追加到m_frames列表中
}

#ifdef SERIAL_STUDIO_INCLUDE_MOC
#    include "moc_Export.cpp"
#endif
