/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
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
 *
 **************************************************************************/

#include <QFileDialog>
#include <QPalette>
#include <QProcess>
#include <QToolButton>
#include <QMessageBox>
#include <QCoreApplication>

#include "ui_vogleditor.h"
#include "vogleditor.h"

#include "vogleditor_qapicalltreemodel.h"
#include "vogleditor_apicalltimelinemodel.h"

#include "vogl_file_utils.h"

#include "vogleditor_output.h"
#include "vogleditor_settings.h"
#include "vogleditor_statetreearbprogramitem.h"
#include "vogleditor_statetreeprogramitem.h"
#include "vogleditor_statetreebufferitem.h"
#include "vogleditor_statetreeshaderitem.h"
#include "vogleditor_statetreeframebufferitem.h"
#include "vogleditor_statetreetextureitem.h"
#include "vogleditor_qbufferexplorer.h"
#include "vogleditor_qstatetreemodel.h"
#include "vogleditor_qtrimdialog.h"
#include "vogleditor_qframebufferexplorer.h"
#include "vogleditor_qprogramarbexplorer.h"
#include "vogleditor_qprogramexplorer.h"
#include "vogleditor_qshaderexplorer.h"
#include "vogleditor_qtimelineview.h"
#include "vogleditor_apicalltreeitem.h"
#include "vogleditor_frameitem.h"

#define VOGLEDITOR_DISABLE_STATE_TAB(tab) ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(tab), false);
#define VOGLEDITOR_ENABLE_STATE_TAB(tab) ui->tabWidget->setTabEnabled(ui->tabWidget->indexOf(tab), true);

#define VOGLEDITOR_DISABLE_BOTTOM_TAB(tab) ui->bottomTabWidget->setTabEnabled(ui->bottomTabWidget->indexOf(tab), false);
#define VOGLEDITOR_ENABLE_BOTTOM_TAB(tab) ui->bottomTabWidget->setTabEnabled(ui->bottomTabWidget->indexOf(tab), true);

//----------------------------------------------------------------------------------------------------------------------
// globals
//----------------------------------------------------------------------------------------------------------------------
static void *g_actual_libgl_module_handle;
static QString g_PROJECT_NAME = "Vogl Editor";
static vogleditor_settings g_settings;
static const char* g_SETTINGS_FILE = "./vogleditor_settings.json";

//----------------------------------------------------------------------------------------------------------------------
// vogl_get_proc_address_helper
//----------------------------------------------------------------------------------------------------------------------
static vogl_void_func_ptr_t vogl_get_proc_address_helper(const char *pName)
{
   VOGL_FUNC_TRACER

   vogl_void_func_ptr_t pFunc = g_actual_libgl_module_handle ? reinterpret_cast<vogl_void_func_ptr_t>(dlsym(g_actual_libgl_module_handle, pName)) : NULL;

   if ((!pFunc) && (GL_ENTRYPOINT(glXGetProcAddress)))
      pFunc = reinterpret_cast<vogl_void_func_ptr_t>( GL_ENTRYPOINT(glXGetProcAddress)(reinterpret_cast<const GLubyte*>(pName)) );

   return pFunc;
}


//----------------------------------------------------------------------------------------------------------------------
// load_gl
//----------------------------------------------------------------------------------------------------------------------
static bool load_gl()
{
   VOGL_FUNC_TRACER

   g_actual_libgl_module_handle = dlopen("libGL.so.1", RTLD_LAZY);
   if (!g_actual_libgl_module_handle)
   {
      vogl_error_printf("%s: Failed loading libGL.so.1!\n", VOGL_FUNCTION_INFO_CSTR);
      return false;
   }

   GL_ENTRYPOINT(glXGetProcAddress) = reinterpret_cast<glXGetProcAddress_func_ptr_t>(dlsym(g_actual_libgl_module_handle, "glXGetProcAddress"));
   if (!GL_ENTRYPOINT(glXGetProcAddress))
   {
      vogl_error_printf("%s: Failed getting address of glXGetProcAddress() from libGL.so.1!\n", VOGL_FUNCTION_INFO_CSTR);
      return false;
   }

   return true;
}

VoglEditor::VoglEditor(QWidget *parent) :
   QMainWindow(parent),
   ui(new Ui::VoglEditor),
   m_pFramebufferExplorer(NULL),
   m_pTextureExplorer(NULL),
   m_pRenderbufferExplorer(NULL),
   m_pProgramExplorer(NULL),
   m_pShaderExplorer(NULL),
   m_pBufferExplorer(NULL),
   m_timeline(NULL),
   m_pFramebufferTab_layout(NULL),
   m_pTextureTab_layout(NULL),
   m_pRenderbufferTab_layout(NULL),
   m_pProgramTab_layout(NULL),
   m_pShaderTab_layout(NULL),
   m_pBufferTab_layout(NULL),
   m_currentSnapshot(NULL),
   m_pCurrentCallTreeItem(NULL),
   m_pVoglReplayProcess(new QProcess()),
   m_pPlayButton(NULL),
   m_pTrimButton(NULL),
   m_pTraceReader(NULL),
   m_pTimelineModel(NULL),
   m_pApiCallTreeModel(NULL),
   m_pStateTreeModel(NULL),
   m_bDelayUpdateUIForContext(false)
{
   ui->setupUi(this);

   if (load_gl())
   {
      vogl_init_actual_gl_entrypoints(vogl_get_proc_address_helper);
   }

   // load the settings file. This will only succeed if the file already exists
   g_settings.load(g_SETTINGS_FILE);

   // always save/resave the file wiill either be created or so that new settings will be added
   g_settings.save(g_SETTINGS_FILE);

   this->move(g_settings.window_position_left(), g_settings.window_position_top());
   this->resize(g_settings.window_size_width(), g_settings.window_size_height());

   vogleditor_output_init(ui->outputTextEdit);
   vogleditor_output_message("Welcome to VoglEditor!");

   // cache the original background color of the search text box
   m_searchTextboxBackgroundColor = ui->searchTextBox->palette().base().color();

   // setup framebuffer tab
   m_pFramebufferTab_layout = new QGridLayout();
   m_pFramebufferExplorer = new vogleditor_QFramebufferExplorer(ui->framebufferTab);
   m_pFramebufferTab_layout->addWidget(m_pFramebufferExplorer, 0, 0);
   ui->framebufferTab->setLayout(m_pFramebufferTab_layout);

   // setup texture tab
   m_pTextureTab_layout = new QGridLayout();
   m_pTextureExplorer = new vogleditor_QTextureExplorer(ui->textureTab);
   m_pTextureTab_layout->addWidget(m_pTextureExplorer, 0, 0);
   ui->textureTab->setLayout(m_pTextureTab_layout);

   // setup renderbuffer tab
   m_pRenderbufferTab_layout = new QGridLayout();
   m_pRenderbufferExplorer = new vogleditor_QTextureExplorer(ui->renderbufferTab);
   m_pRenderbufferTab_layout->addWidget(m_pRenderbufferExplorer, 0, 0);
   ui->renderbufferTab->setLayout(m_pRenderbufferTab_layout);

   // setup program tab
   m_pProgramTab_layout = new QGridLayout();
   m_pProgramExplorer = new vogleditor_QProgramExplorer(ui->programTab);
   m_pProgramTab_layout->addWidget(m_pProgramExplorer, 0, 0);
   ui->programTab->setLayout(m_pProgramTab_layout);

   // setup program ARB tab
   m_pProgramArbTab_layout = new QGridLayout();
   m_pProgramArbExplorer = new vogleditor_QProgramArbExplorer(ui->programArbTab);
   m_pProgramArbTab_layout->addWidget(m_pProgramArbExplorer, 0, 0);
   ui->programArbTab->setLayout(m_pProgramArbTab_layout);

   // setup shader tab
   m_pShaderTab_layout = new QGridLayout();
   m_pShaderExplorer = new vogleditor_QShaderExplorer(ui->shaderTab);
   m_pShaderTab_layout->addWidget(m_pShaderExplorer, 0, 0);
   ui->shaderTab->setLayout(m_pShaderTab_layout);

   // setup buffer tab
   m_pBufferTab_layout = new QGridLayout();
   m_pBufferExplorer = new vogleditor_QBufferExplorer(ui->bufferTab);
   m_pBufferTab_layout->addWidget(m_pBufferExplorer, 0, 0);
   ui->bufferTab->setLayout(m_pBufferTab_layout);

   // setup timeline
   m_timeline = new vogleditor_QTimelineView();
   m_timeline->setMinimumHeight(100);
   ui->timelineLayout->addWidget(m_timeline);
   ui->timelineLayout->removeWidget(ui->timelineViewPlaceholder);
   delete ui->timelineViewPlaceholder;
   ui->timelineViewPlaceholder = NULL;

   // add buttons to toolbar
   m_pPlayButton = new QToolButton(ui->mainToolBar);
   m_pPlayButton->setText("Play Trace");
   m_pPlayButton->setEnabled(false);

   m_pTrimButton = new QToolButton(ui->mainToolBar);
   m_pTrimButton->setText("Trim Trace");
   m_pTrimButton->setEnabled(false);

   ui->mainToolBar->addWidget(m_pPlayButton);
   ui->mainToolBar->addWidget(m_pTrimButton);

   connect(m_pPlayButton, SIGNAL(clicked()), this, SLOT(playCurrentTraceFile()));
   connect(m_pTrimButton, SIGNAL(clicked()), this, SLOT(trimCurrentTraceFile()));

   connect(m_pProgramArbExplorer, SIGNAL(program_edited(vogl_arb_program_state*)), this, SLOT(slot_program_edited(vogl_arb_program_state*)));
   connect(m_pProgramExplorer, SIGNAL(program_edited(vogl_program_state*)), this, SLOT(slot_program_edited(vogl_program_state*)));

   connect(m_pVoglReplayProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(slot_readReplayStandardOutput()));
   connect(m_pVoglReplayProcess, SIGNAL(readyReadStandardError()), this, SLOT(slot_readReplayStandardError()));

   reset_tracefile_ui();
}

VoglEditor::~VoglEditor()
{
    // update any settings and save the settings file
    g_settings.set_window_position_left(this->x());
    g_settings.set_window_position_top(this->y());
    g_settings.set_window_size_width(this->width());
    g_settings.set_window_size_height(this->height());
    g_settings.save(g_SETTINGS_FILE);

    close_trace_file();
    delete ui;
    vogleditor_output_deinit();

    if (m_pFramebufferExplorer != NULL)
    {
        delete m_pFramebufferExplorer;
        m_pFramebufferExplorer = NULL;
    }

    if (m_pTextureExplorer != NULL)
    {
        delete m_pTextureExplorer;
        m_pTextureExplorer = NULL;
    }

    if (m_pRenderbufferExplorer != NULL)
    {
        delete m_pRenderbufferExplorer;
        m_pRenderbufferExplorer = NULL;
    }

    if (m_pProgramArbExplorer != NULL)
    {
        delete m_pProgramArbExplorer;
        m_pProgramArbExplorer = NULL;
    }

    if (m_pProgramExplorer != NULL)
    {
        delete m_pProgramExplorer;
        m_pProgramExplorer = NULL;
    }

    if (m_pShaderExplorer != NULL)
    {
        delete m_pShaderExplorer;
        m_pShaderExplorer = NULL;
    }

    if (m_pBufferExplorer != NULL)
    {
        delete m_pBufferExplorer;
        m_pBufferExplorer = NULL;
    }

    if (m_pPlayButton != NULL)
    {
        delete m_pPlayButton;
        m_pPlayButton = NULL;
    }

    if (m_pTrimButton != NULL)
    {
        delete m_pTrimButton;
        m_pTrimButton = NULL;
    }

    if (m_pFramebufferTab_layout != NULL)
    {
        delete m_pFramebufferTab_layout;
        m_pFramebufferTab_layout = NULL;
    }

    if (m_pTextureTab_layout != NULL)
    {
        delete m_pTextureTab_layout;
        m_pTextureTab_layout = NULL;
    }

    if (m_pRenderbufferTab_layout != NULL)
    {
        delete m_pRenderbufferTab_layout;
        m_pRenderbufferTab_layout = NULL;
    }

    if (m_pProgramArbTab_layout != NULL)
    {
        delete m_pProgramArbTab_layout;
        m_pProgramArbTab_layout = NULL;
    }

    if (m_pProgramTab_layout != NULL)
    {
        delete m_pProgramTab_layout;
        m_pProgramTab_layout = NULL;
    }

    if (m_pShaderTab_layout != NULL)
    {
        delete m_pShaderTab_layout;
        m_pShaderTab_layout = NULL;
    }

    if (m_pBufferTab_layout != NULL)
    {
        delete m_pBufferTab_layout;
        m_pBufferTab_layout = NULL;
    }

    if (m_pStateTreeModel != NULL)
    {
        delete m_pStateTreeModel;
        m_pStateTreeModel = NULL;
    }

    if (m_pVoglReplayProcess != NULL)
    {
        delete m_pVoglReplayProcess;
        m_pVoglReplayProcess = NULL;
    }
}

void VoglEditor::playCurrentTraceFile()
{
    QCursor origCursor = cursor();
    setCursor(Qt::WaitCursor);

    // update UI
    m_pPlayButton->setEnabled(false);
    m_pTrimButton->setEnabled(false);

    m_traceReplayer.replay(m_pTraceReader, m_pApiCallTreeModel->root(), NULL, 0, true);

    m_pPlayButton->setEnabled(true);
    m_pTrimButton->setEnabled(true);

    setCursor(origCursor);
}

void VoglEditor::trimCurrentTraceFile()
{
    trim_trace_file(m_openFilename, static_cast<uint>(m_pTraceReader->get_max_frame_index()), g_settings.trim_large_trace_prompt_size());
}

/// \return True if the new trim file is now open in the editor
/// \return False if there was an error, or the user elected NOT to open the new trim file
bool VoglEditor::trim_trace_file(QString filename, uint maxFrameIndex, uint maxAllowedTrimLen)
{
    // open a dialog to gather parameters for the replayer
    vogleditor_QTrimDialog trimDialog(filename, maxFrameIndex, maxAllowedTrimLen, this);
    int code = trimDialog.exec();

    if (code == QDialog::Rejected)
    {
        return false;
    }

    QStringList arguments;
    arguments << "--trim_frame" << trimDialog.trim_frame() << "--trim_len" << trimDialog.trim_len() << "--trim_file" << trimDialog.trim_file() << filename;

    QDir appDirectory(QCoreApplication::applicationDirPath());
#ifdef __i386__
    QString executable = appDirectory.absoluteFilePath("./voglreplay32");
#else
    QString executable = appDirectory.absoluteFilePath("./voglreplay64");
#endif

    QString cmdLine = executable + " " + arguments.join(" ");

    vogleditor_output_message("Trimming trace file");
    vogleditor_output_message(cmdLine.toStdString().c_str());
    m_pVoglReplayProcess->start(executable, arguments);
    if (m_pVoglReplayProcess->waitForStarted() == false)
    {
        vogleditor_output_error("voglreplay could not be executed.");
        return false;
    }

    // This is a bad idea as it will wait forever,
    // but if the replay is taking forever then we have bigger problems.
    if(m_pVoglReplayProcess->waitForFinished(-1))
    {
        vogleditor_output_message("Trim Completed!");
    }

    int procRetValue = m_pVoglReplayProcess->exitCode();

    bool bCompleted = false;
    if (procRetValue == -2)
    {
        // proc failed to starts
        vogleditor_output_error("voglreplay could not be executed.");
    }
    else if (procRetValue == -1)
    {
        // proc crashed
        vogleditor_output_error("voglreplay aborted unexpectedly.");
    }
    else if (procRetValue == 0)
    {
        // success
        bCompleted = true;
    }
    else
    {
        // some other return value
        bCompleted = false;
    }

    if (bCompleted)
    {
        int ret = QMessageBox::warning(this, tr("Trim Trace"), tr("Would you like to load the new trimmed trace file?"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (ret == QMessageBox::Yes)
        {
            close_trace_file();
            if (open_trace_file(trimDialog.trim_file().toStdString().c_str()))
            {
                return true;
            }
            else
            {
                vogleditor_output_error("Could not open trace file.");
                QMessageBox::critical(this, tr("Error"), tr("Could not open trace file."));
            }
        }
    }
    else
    {
        vogleditor_output_error("Failed to trim the trace file.");
        QMessageBox::critical(this, tr("Error"), tr("Failed to trim the trace file."));
    }
    return false;
}

void VoglEditor::on_actionE_xit_triggered()
{
   qApp->quit();
}

void VoglEditor::on_action_Open_triggered()
{
   QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"), QString(),
           tr("VOGL Binary Files (*.bin);;VOGL JSON Files (*.json)"));

   if (!fileName.isEmpty()) {
      vogl::dynamic_string filename;
      filename.set(fileName.toStdString().c_str());

      if (open_trace_file(filename) == false) {
          QMessageBox::critical(this, tr("Error"), tr("Could not open trace file."));
          return;
      }
   }
}

void VoglEditor::on_action_Close_triggered()
{
    close_trace_file();
}

void VoglEditor::close_trace_file()
{
   if (m_pTraceReader != NULL)
   {
      vogleditor_output_message("Closing trace file.");
      vogleditor_output_message("-------------------");
      m_pTraceReader->close();
      vogl_delete(m_pTraceReader);
      m_pTraceReader = NULL;

      setWindowTitle(g_PROJECT_NAME);

      m_openFilename.clear();
      m_backtraceToJsonMap.clear();
      m_backtraceDoc.clear();

      reset_tracefile_ui();

      ui->treeView->setModel(NULL);
      ui->machineInfoText->clear();
      ui->backtraceText->clear();
      m_timeline->setModel(NULL);
      m_timeline->repaint();

      if (m_pTimelineModel != NULL)
      {
          delete m_pTimelineModel;
          m_pTimelineModel = NULL;
      }

      if (m_pApiCallTreeModel != NULL)
      {
          delete m_pApiCallTreeModel;
          m_pApiCallTreeModel = NULL;
      }
   }
}

void VoglEditor::write_child_api_calls(vogleditor_apiCallTreeItem* pItem, FILE* pFile)
{
    QString string = pItem->columnData(VOGL_ACTC_APICALL, Qt::DisplayRole).toString();
    vogl_fwrite(string.toStdString().c_str(), 1, string.size(), pFile);
    vogl_fwrite("\r\n", 1, 2, pFile);

    for (int i = 0; i < pItem->childCount(); i++)
    {
        write_child_api_calls(pItem->child(i), pFile);
    }
}

void VoglEditor::on_actionExport_API_Calls_triggered()
{
    QString suggestedName = m_openFilename;

    int lastIndex = suggestedName.lastIndexOf('-');
    if (lastIndex != -1)
    {
        suggestedName = suggestedName.remove(lastIndex, suggestedName.size() - lastIndex);
    }
    suggestedName += "-ApiCalls.txt";

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export API Calls"), suggestedName, tr("Text (*.txt)"));

    if (!fileName.isEmpty())
    {
        vogl::dynamic_string filename;
        filename.set(fileName.toStdString().c_str());

        FILE* pFile = vogl_fopen(filename.c_str(), "w");
        vogleditor_QApiCallTreeModel* pModel = static_cast<vogleditor_QApiCallTreeModel*>(ui->treeView->model());
        vogleditor_apiCallTreeItem* pRoot = pModel->root();
        for (int i = 0; i < pRoot->childCount(); i++)
        {
            write_child_api_calls(pRoot->child(i), pFile);
        }
        vogl_fclose(pFile);
    }
}

static const unsigned int VOGLEDITOR_SESSION_FILE_FORMAT_VERSION_1 = 1;
static const unsigned int VOGLEDITOR_SESSION_FILE_FORMAT_VERSION = VOGLEDITOR_SESSION_FILE_FORMAT_VERSION_1;

bool VoglEditor::load_session_from_disk(QString sessionFile)
{
    // open the json doc
    json_document sessionDoc;
    if (!sessionDoc.deserialize_file(sessionFile.toStdString().c_str()))
    {
        return false;
    }

    // look for expected metadata
    json_node* pMetadata = sessionDoc.get_root()->find_child_object("metadata");
    if (pMetadata == NULL)
    {
        return false;
    }

    const json_value& rFormatVersion = pMetadata->find_value("session_file_format_version");
    if (!rFormatVersion.is_valid())
    {
        return false;
    }

    if (rFormatVersion.as_uint32() != VOGLEDITOR_SESSION_FILE_FORMAT_VERSION_1)
    {
        return false;
    }

    // load base trace file
    json_node* pBaseTraceFile = sessionDoc.get_root()->find_child_object("base_trace_file");
    if (pBaseTraceFile == NULL)
    {
        return false;
    }

    const json_value& rBaseTraceFilePath = pBaseTraceFile->find_value("rel_path");
    const json_value& rBaseTraceFileUuid = pBaseTraceFile->find_value("uuid");

    if (!rBaseTraceFilePath.is_valid() || !rBaseTraceFileUuid.is_valid())
    {
        return false;
    }

    dynamic_string sessionPathName;
    dynamic_string sessionFileName;
    file_utils::split_path(sessionFile.toStdString().c_str(), sessionPathName, sessionFileName);

    dynamic_string traceFilePath = sessionPathName;
    traceFilePath.append(rBaseTraceFilePath.as_string());

    if (!open_trace_file(traceFilePath))
    {
        return false;
    }

    // TODO: verify UUID of the loaded trace file

    // load session data if it is available
    json_node* pSessionData = sessionDoc.get_root()->find_child_object("session_data");
    if (pSessionData != NULL)
    {
        const json_value& rSessionPath = pSessionData->find_value("rel_path");
        if (!rSessionPath.is_valid())
        {
            return false;
        }

        dynamic_string sessionDataPath = sessionPathName;
        sessionDataPath.append(rSessionPath.as_string());

        vogl_loose_file_blob_manager file_blob_manager;
        file_blob_manager.init(cBMFReadWrite, sessionDataPath.c_str());
        vogl_blob_manager* pBlob_manager = static_cast<vogl_blob_manager*>(&file_blob_manager);

        // load snapshots
        const json_node* pSnapshots = pSessionData->find_child_array("snapshots");
        for (unsigned int i = 0; i < pSnapshots->size(); i++)
        {
            const json_node* pSnapshotNode = pSnapshots->get_value_as_object(i);

            const json_value& uuid = pSnapshotNode->find_value("uuid");
            const json_value& isValid = pSnapshotNode->find_value("is_valid");
            const json_value& isEdited = pSnapshotNode->find_value("is_edited");
            const json_value& isOutdated = pSnapshotNode->find_value("is_outdated");
            const json_value& frameNumber = pSnapshotNode->find_value("frame_number");
            const json_value& callIndex = pSnapshotNode->find_value("call_index");
            const json_value& path = pSnapshotNode->find_value("rel_path");

            // make sure expected nodes are valid
            if (!isValid.is_valid() || !isEdited.is_valid() || !isOutdated.is_valid())
            {
                return false;
            }

            vogl_gl_state_snapshot* pSnapshot = NULL;

            if (path.is_valid() && isValid.as_bool() && uuid.is_valid())
            {
                dynamic_string snapshotPath = sessionDataPath;
                snapshotPath.append(path.as_string());

                // load the snapshot
                json_document snapshotDoc;
                if (!snapshotDoc.deserialize_file(snapshotPath.c_str()))
                {
                    return false;
                }

                // attempt to verify the snapshot file
                json_node* pSnapshotRoot = snapshotDoc.get_root();
                if (pSnapshotRoot == NULL)
                {
                    vogl_warning_printf("Invalid snapshot file at %s.", path.as_string_ptr());
                    continue;
                }

                const json_value& snapshotUuid = pSnapshotRoot->find_value("uuid");
                if (!snapshotUuid.is_valid())
                {
                    vogl_warning_printf("Invalid 'uuid' in snapshot file at %s.", path.as_string_ptr());
                    continue;
                }

                if (snapshotUuid.as_string() != uuid.as_string())
                {
                    vogl_warning_printf("Mismatching 'uuid' between snapshot file at %s and that stored in the session file at %s.", path.as_string_ptr(), sessionFile.toStdString().c_str());
                    continue;
                }

                vogl_ctypes trace_gl_ctypes(m_pTraceReader->get_sof_packet().m_pointer_sizes);
                pSnapshot = vogl_new(vogl_gl_state_snapshot);
                if (!pSnapshot->deserialize(*snapshotDoc.get_root(), *pBlob_manager, &trace_gl_ctypes))
                {
                    vogl_delete(pSnapshot);
                    pSnapshot = NULL;
                    vogl_warning_printf("Unable to deserialize the snapshot with uuid %s.", uuid.as_string_ptr());
                    continue;
                }
            }

            vogleditor_gl_state_snapshot* pContainer = vogl_new(vogleditor_gl_state_snapshot, pSnapshot);
            pContainer->set_edited(isEdited.as_bool());
            pContainer->set_outdated(isOutdated.as_bool());

            if (callIndex.is_valid())
            {
                // the snapshot is associated with an api call
                vogleditor_apiCallTreeItem* pItem = m_pApiCallTreeModel->find_call_number(callIndex.as_uint64());
                if (pItem != NULL)
                {
                    pItem->set_snapshot(pContainer);
                }
                else
                {
                    vogl_warning_printf("Unable to find API call index %" PRIu64 " to load the snapshot into.", callIndex.as_uint64());
                    if (pSnapshot != NULL) { vogl_delete(pSnapshot); pSnapshot = NULL; }
                    if (pContainer != NULL) { vogl_delete(pContainer); pContainer = NULL; }
                }
            }
            else if (frameNumber.is_valid())
            {
                // the snapshot is associated with a frame.
                // frame snapshots have the additional requirement that the snapshot itself MUST exist since
                // we only save a frame snapshot if it is the inital frame and it has been edited.
                // If we allow NULL snapshots, that we could accidently remove the initial snapshot that was loaded with the trace file.
                if (pSnapshot != NULL)
                {
                    vogleditor_apiCallTreeItem* pItem = m_pApiCallTreeModel->find_frame_number(frameNumber.as_uint64());
                    if (pItem != NULL)
                    {
                        pItem->set_snapshot(pContainer);
                    }
                    else
                    {
                        vogl_warning_printf("Unable to find frame number %" PRIu64 " to load the snapshot into.", frameNumber.as_uint64());
                        if (pSnapshot != NULL) { vogl_delete(pSnapshot); pSnapshot = NULL; }
                        if (pContainer != NULL) { vogl_delete(pContainer); pContainer = NULL; }
                    }
                }
            }
            else
            {
                vogl_warning_printf("Session file contains invalid call or frame number for snapshot with uuid %s", uuid.as_string_ptr());
                if (pSnapshot != NULL) { vogl_delete(pSnapshot); pSnapshot = NULL; }
                if (pContainer != NULL) { vogl_delete(pContainer); pContainer = NULL; }
            }
        }
    }

    return true;
}

/*
 * Below is a summary of the information that needs to be saved out in a session's json file so that we can reload the session and be fully-featured.
 * Note that not all of this information is currently supported (either by VoglEditor or the save/load functionality).
 *
 * sample data structure for version 1:
{
   "metadata" : {
      "session_file_format_version" : "0x1"  <- would need to be updated when organization of existing data is changed
   },
   "base_trace_file" : {
      "path" : "../traces/trimmed4.bin",
      "uuid" : [ 2761638124, 1361789091, 2623121922, 1789156619 ]
   },
   "session_data" : {
      "path" : "/home/peterl/voglproj/vogl_build/traces/trimmed4-vogleditor-sessiondata/",
      "snapshots" : [
         {
            "uuid" : "B346B680801ED2F5144E421DEA5EFDCC",
            "is_valid" : true,
            "is_edited" : false,
            "is_outdated" : false,
            "frame_number" : 0
         },
         {
            "uuid" : "BC261B884088DBEADF376A03A489F2B9",
            "is_valid" : true,
            "is_edited" : false,
            "is_outdated" : false,
            "call_index" : 881069,
            "path" : "/home/peterl/voglproj/vogl_build/traces/trimmed4-vogleditor-sessiondata/snapshot_call_881069.json"
         },
         {
            "uuid" : "176DE3DEAA437B871FE122C84D5432E3",
            "is_valid" : true,
            "is_edited" : true,
            "is_outdated" : false,
            "call_index" : 881075,
            "path" : "/home/peterl/voglproj/vogl_build/traces/trimmed4-vogleditor-sessiondata/snapshot_call_881075.json"
         },
         {
            "is_valid" : false,
            "is_edited" : false,
            "is_outdated" : true,
            "call_index" : 881080
         }
      ]
   }
}
*/
bool VoglEditor::save_session_to_disk(QString sessionFile)
{
    dynamic_string sessionPathName;
    dynamic_string sessionFileName;
    file_utils::split_path(sessionFile.toStdString().c_str(), sessionPathName, sessionFileName);

    // modify the session file name to make a sessiondata folder
    QString sessionDataFolder(sessionFileName.c_str());
    int lastIndex = sessionDataFolder.lastIndexOf('.');
    if (lastIndex != -1)
    {
        sessionDataFolder = sessionDataFolder.remove(lastIndex, sessionDataFolder.size() - lastIndex);
    }
    sessionDataFolder += "-sessiondata/";

    dynamic_string sessionDataPath = sessionPathName;
    sessionDataPath.append(sessionDataFolder.toStdString().c_str());
    file_utils::create_directories(sessionDataPath, false);

    vogl_loose_file_blob_manager file_blob_manager;
    file_blob_manager.init(cBMFReadWrite, sessionDataPath.c_str());
    vogl_blob_manager* pBlob_manager = static_cast<vogl_blob_manager*>(&file_blob_manager);

    QCursor origCursor = this->cursor();
    setCursor(Qt::WaitCursor);

    json_document sessionDoc;
    json_node& metadata = sessionDoc.get_root()->add_object("metadata");
    metadata.add_key_value("session_file_format_version", to_hex_string(VOGLEDITOR_SESSION_FILE_FORMAT_VERSION));

    // find relative path from session file to trace file
    QDir relativeAppDir;
    QString absoluteTracePath = relativeAppDir.absoluteFilePath(m_openFilename.toStdString().c_str());
    QDir absoluteSessionFileDir(sessionPathName.c_str());
    QString tracePathRelativeToSessionFile = absoluteSessionFileDir.relativeFilePath(absoluteTracePath);

    json_node& baseTraceFile = sessionDoc.get_root()->add_object("base_trace_file");
    baseTraceFile.add_key_value("rel_path", tracePathRelativeToSessionFile.toStdString().c_str());
    json_node &uuid_array = baseTraceFile.add_array("uuid");
    for (uint i = 0; i < VOGL_ARRAY_SIZE(m_pTraceReader->get_sof_packet().m_uuid); i++)
    {
        uuid_array.add_value(m_pTraceReader->get_sof_packet().m_uuid[i]);
    }

    json_node& sessionDataNode = sessionDoc.get_root()->add_object("session_data");
    sessionDataNode.add_key_value("rel_path", sessionDataFolder.toStdString().c_str());
    json_node& snapshotArray = sessionDataNode.add_array("snapshots");

    vogleditor_apiCallTreeItem* pItem = m_pApiCallTreeModel->find_next_snapshot(NULL);
    vogleditor_apiCallTreeItem* pLastItem = NULL;
    bool bSavedSuccessfully = true;
    while (pItem != pLastItem && pItem != NULL)
    {
        dynamic_string filename;

        json_node& snapshotNode = snapshotArray.add_object();
        if (pItem->get_snapshot()->get_snapshot() != NULL)
        {
            dynamic_string strUUID;
            snapshotNode.add_key_value("uuid", pItem->get_snapshot()->get_snapshot()->get_uuid().get_string(strUUID));
        }
        snapshotNode.add_key_value("is_valid", pItem->get_snapshot()->is_valid());
        snapshotNode.add_key_value("is_edited", pItem->get_snapshot()->is_edited());
        snapshotNode.add_key_value("is_outdated", pItem->get_snapshot()->is_outdated());

        if (pItem->apiCallItem() != NULL)
        {
            uint64_t callIndex = pItem->apiCallItem()->globalCallIndex();
            snapshotNode.add_key_value("call_index", callIndex);
            if (pItem->get_snapshot()->get_snapshot() != NULL)
            {
                filename = filename.format("snapshot_call_%" PRIu64 ".json", callIndex);
                snapshotNode.add_key_value("rel_path", filename);
                dynamic_string filepath = sessionDataPath;
                filepath.append(filename);
                if (!save_snapshot_to_disk(pItem->get_snapshot()->get_snapshot(), filepath, pBlob_manager))
                {
                    bSavedSuccessfully = false;
                    break;
                }
            }
        }
        else if (pItem->frameItem() != NULL)
        {
            // the first frame of a trim will have a snapshot.
            // this should only be saved out if the snapshot has been edited
            uint64_t frameNumber = pItem->frameItem()->frameNumber();
            snapshotNode.add_key_value("frame_number", frameNumber);
            if (pItem->get_snapshot()->is_edited())
            {
                filename = filename.format("snapshot_frame_%" PRIu64 ".json", frameNumber);
                snapshotNode.add_key_value("rel_path", filename);
                dynamic_string filepath = sessionDataPath;
                filepath.append(filename);
                if (!save_snapshot_to_disk(pItem->get_snapshot()->get_snapshot(), filepath, pBlob_manager))
                {
                    bSavedSuccessfully = false;
                    break;
                }
            }
        }

        pLastItem = pItem;
        pItem = m_pApiCallTreeModel->find_next_snapshot(pLastItem);
    }

    if (bSavedSuccessfully)
    {
        bSavedSuccessfully = sessionDoc.serialize_to_file(sessionFile.toStdString().c_str());
    }

    setCursor(origCursor);

    return bSavedSuccessfully;
}

bool VoglEditor::save_snapshot_to_disk(vogl_gl_state_snapshot *pSnapshot, dynamic_string filename, vogl_blob_manager *pBlob_manager)
{
    if (pSnapshot == NULL)
    {
        return false;
    }

    json_document doc;

    vogl_ctypes trace_gl_ctypes(m_pTraceReader->get_sof_packet().m_pointer_sizes);

    if (!pSnapshot->serialize(*doc.get_root(), *pBlob_manager, &trace_gl_ctypes))
    {
        vogl_error_printf("Failed serializing state snapshot document!\n");
        return false;
    }
    else if (!doc.serialize_to_file(filename.get_ptr(), true))
    {
        vogl_error_printf("Failed writing state snapshot to file \"%s\"!\n", filename.get_ptr());
        return false;
    }
    else
    {
        vogl_printf("Successfully wrote JSON snapshot to file \"%s\"\n", filename.get_ptr());
    }

    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// read_state_snapshot_from_trace
//----------------------------------------------------------------------------------------------------------------------
vogl_gl_state_snapshot* VoglEditor::read_state_snapshot_from_trace(vogl_trace_file_reader* pTrace_reader)
{
   vogl_ctypes trace_gl_ctypes(pTrace_reader->get_sof_packet().m_pointer_sizes);

   vogl_trace_packet keyframe_trace_packet(&trace_gl_ctypes);

   pTrace_reader->seek_to_frame(0);

   vogl_gl_state_snapshot *pSnapshot = NULL;
   bool found_snapshot = false;
   do
   {
      vogl_trace_file_reader::trace_file_reader_status_t read_status = pTrace_reader->read_next_packet();

      if ((read_status != vogl_trace_file_reader::cOK) && (read_status != vogl_trace_file_reader::cEOF))
      {
         vogl_error_printf("%s: Failed reading from keyframe trace file!\n", VOGL_FUNCTION_INFO_CSTR);
         return NULL;
      }

      if ((read_status == vogl_trace_file_reader::cEOF) || (pTrace_reader->get_packet_type() == cTSPTEOF))
      {
         vogl_error_printf("%s: Failed finding state snapshot in keyframe file!\n", VOGL_FUNCTION_INFO_CSTR);
         return NULL;
      }

      if (pTrace_reader->get_packet_type() != cTSPTGLEntrypoint)
         continue;

      if (!keyframe_trace_packet.deserialize(pTrace_reader->get_packet_buf().get_ptr(), pTrace_reader->get_packet_buf().size(), false))
      {
         vogl_error_printf("%s: Failed parsing GL entrypoint packet in keyframe file\n", VOGL_FUNCTION_INFO_CSTR);
         return NULL;
      }

      const vogl_trace_gl_entrypoint_packet *pGL_packet = &pTrace_reader->get_packet<vogl_trace_gl_entrypoint_packet>();
      gl_entrypoint_id_t entrypoint_id = static_cast<gl_entrypoint_id_t>(pGL_packet->m_entrypoint_id);

      if (vogl_is_swap_buffers_entrypoint(entrypoint_id) || vogl_is_draw_entrypoint(entrypoint_id) || vogl_is_make_current_entrypoint(entrypoint_id))
      {
         vogl_error_printf("Failed finding state snapshot in keyframe file!\n");
         return NULL;
      }

      switch (entrypoint_id)
      {
         case VOGL_ENTRYPOINT_glInternalTraceCommandRAD:
         {
            GLuint cmd = keyframe_trace_packet.get_param_value<GLuint>(0);
            GLuint size = keyframe_trace_packet.get_param_value<GLuint>(1); VOGL_NOTE_UNUSED(size);

            if (cmd == cITCRKeyValueMap)
            {
               key_value_map &kvm = keyframe_trace_packet.get_key_value_map();

               dynamic_string cmd_type(kvm.get_string("command_type"));
               if (cmd_type == "state_snapshot")
               {
                  dynamic_string id(kvm.get_string("binary_id"));
                  if (id.is_empty())
                  {
                     vogl_error_printf("%s: Missing binary_id field in glInternalTraceCommandRAD key_valye_map command type: \"%s\"\n", VOGL_FUNCTION_INFO_CSTR, cmd_type.get_ptr());
                     return NULL;
                  }

                  uint8_vec snapshot_data;
                  {
                     timed_scope ts("get_multi_blob_manager().get");
                     if (!pTrace_reader->get_multi_blob_manager().get(id, snapshot_data) || (snapshot_data.is_empty()))
                     {
                        vogl_error_printf("%s: Failed reading snapshot blob data \"%s\"!\n", VOGL_FUNCTION_INFO_CSTR, id.get_ptr());
                        return NULL;
                     }
                  }

                  vogl_message_printf("%s: Deserializing state snapshot \"%s\", %u bytes\n", VOGL_FUNCTION_INFO_CSTR, id.get_ptr(), snapshot_data.size());

                  json_document doc;
                  {
                     timed_scope ts("doc.binary_deserialize");
                     if (!doc.binary_deserialize(snapshot_data) || (!doc.get_root()))
                     {
                        vogl_error_printf("%s: Failed deserializing JSON snapshot blob data \"%s\"!\n", VOGL_FUNCTION_INFO_CSTR, id.get_ptr());
                        return NULL;
                     }
                  }

                  pSnapshot = vogl_new(vogl_gl_state_snapshot);

                  timed_scope ts("pSnapshot->deserialize");
                  if (!pSnapshot->deserialize(*doc.get_root(), pTrace_reader->get_multi_blob_manager(), &trace_gl_ctypes))
                  {
                     vogl_delete(pSnapshot);
                     pSnapshot = NULL;

                     vogl_error_printf("%s: Failed deserializing snapshot blob data \"%s\"!\n", VOGL_FUNCTION_INFO_CSTR, id.get_ptr());
                     return NULL;
                  }

                  found_snapshot = true;
               }
            }

            break;
         }
         default: break;
      }

   } while (!found_snapshot);

   return pSnapshot;
}

bool VoglEditor::open_trace_file(dynamic_string filename)
{
   QCursor origCursor = this->cursor();
   this->setCursor(Qt::WaitCursor);

   vogl_loose_file_blob_manager file_blob_manager;
   dynamic_string keyframe_trace_path(file_utils::get_pathname(filename.get_ptr()));
   file_blob_manager.init(cBMFReadable, keyframe_trace_path.get_ptr());

   dynamic_string actual_keyframe_filename;

   vogleditor_output_message("*********************");
   vogleditor_output_message("Opening trace file...");
   vogleditor_output_message(filename.c_str());

   vogl_trace_file_reader* tmpReader = vogl_open_trace_file(filename, actual_keyframe_filename, NULL);

   if (tmpReader == NULL)
   {
      vogleditor_output_error("Unable to open trace file.");
      this->setCursor(origCursor);
      return false;
   }

   if (tmpReader->get_max_frame_index() > g_settings.trim_large_trace_prompt_size())
   {
       int ret = QMessageBox::warning(this, tr(g_PROJECT_NAME.toStdString().c_str()), tr("The loaded trace file has many frames and debugging may be difficult.\nWould you like to trim the trace?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

       if (ret == QMessageBox::Yes)
       {
           if (trim_trace_file(filename.c_str(), static_cast<uint>(tmpReader->get_max_frame_index()), g_settings.trim_large_trace_prompt_size()))
           {
               // user decided to open the new trim file, and the UI should already be updated
               // clean up here and return
               vogl_delete(tmpReader);
               this->setCursor(origCursor);
               return true;
           }
           else
           {
               // either there was an error, or the user decided NOT to open the trim file,
               // by the time the user gets to this point, it really doesn't feel like the original file should be opened, so just clean up and return
               vogl_delete(tmpReader);
               this->setCursor(origCursor);
               vogleditor_output_error("Trimmed trace file will not be opened.");
               vogleditor_output_message("-------------------");
               return false;
           }
       }
       else
       {
           // either there was an error, or the user decided NOT to open the trim file,
           // so continue to load the original file
           vogleditor_output_warning("Large trace files may be difficult to debug.");
       }
   }

   // now that we know the new trace file can be opened,
   // close the old one, and update the trace reader
   close_trace_file();
   m_pTraceReader = tmpReader;

   vogl_ctypes trace_ctypes;
   trace_ctypes.init(m_pTraceReader->get_sof_packet().m_pointer_sizes);

   m_pApiCallTreeModel = new vogleditor_QApiCallTreeModel();
   if (m_pApiCallTreeModel == NULL)
   {
       vogleditor_output_error("Out of memory.");
       close_trace_file();
       this->setCursor(origCursor);
       return false;
   }

   if (!m_pApiCallTreeModel->init(m_pTraceReader))
   {
      vogleditor_output_error("The API calls within the trace could not be parsed properly.");
      close_trace_file();
      this->setCursor(origCursor);
      return false;
   }

   ui->treeView->setModel(m_pApiCallTreeModel);

   if (ui->treeView->selectionModel() != NULL)
   {
      connect(ui->treeView->selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(slot_treeView_currentChanged(const QModelIndex &, const QModelIndex &)));
   }

   if (m_pApiCallTreeModel->hasChildren())
   {
      ui->treeView->setExpanded(m_pApiCallTreeModel->index(0,0), true);
      ui->treeView->setCurrentIndex(m_pApiCallTreeModel->index(0,0));
   }

   int flagsColumnWidth = 30;
   ui->treeView->header()->setMinimumSectionSize(flagsColumnWidth);
   ui->treeView->header()->moveSection(VOGL_ACTC_FLAGS, 0);
   ui->treeView->setColumnWidth(VOGL_ACTC_FLAGS, flagsColumnWidth);

   int width = ui->treeView->width() - flagsColumnWidth - 30; // subtract a little extra for the scrollbar width
   ui->treeView->setColumnWidth(VOGL_ACTC_APICALL, width * 0.7);
   ui->treeView->setColumnWidth(VOGL_ACTC_INDEX, width * 0.15);
   ui->treeView->setColumnWidth(VOGL_ACTC_DURATION, width * 0.15);

   ui->searchTextBox->setEnabled(true);
   ui->searchPrevButton->setEnabled(true);
   ui->searchNextButton->setEnabled(true);

   ui->action_Close->setEnabled(true);
   ui->actionSave_Session->setEnabled(true);
   ui->actionExport_API_Calls->setEnabled(true);

   ui->prevSnapshotButton->setEnabled(true);
   ui->nextSnapshotButton->setEnabled(true);
   ui->prevDrawcallButton->setEnabled(true);
   ui->nextDrawcallButton->setEnabled(true);

   m_backtraceToJsonMap.clear();
   m_backtraceDoc.clear();

    // Extract backtrace map and machine info from trace archive
    if (m_pTraceReader->get_archive_blob_manager().is_initialized())
    {
        // backtrace
        uint8_vec backtrace_data;
        bool bBacktraceVisible = false;
        if (m_pTraceReader->get_archive_blob_manager().does_exist(VOGL_TRACE_ARCHIVE_BACKTRACE_MAP_ADDRS_FILENAME))
        {
            //$ TODO mikesart: read MAP_SYMS data here when symbols have been resolved.
            if (m_pTraceReader->get_archive_blob_manager().get(VOGL_TRACE_ARCHIVE_BACKTRACE_MAP_ADDRS_FILENAME, backtrace_data))
            {
                json_node* pRoot = m_backtraceDoc.get_root();
                if (m_backtraceDoc.deserialize((const char*)backtrace_data.get_ptr(), backtrace_data.size()))
                {
                    bBacktraceVisible = pRoot->size() > 0;
                    for (uint i = 0; i < pRoot->size(); i++)
                    {
                        json_node* pChild = pRoot->get_child(i);
                        uint32 index = 0;
                        VOGL_ASSERT("Backtrace node does not have an 'index' child" && pChild != NULL && pChild->get_value_as_uint32("index", index));
                        if (pChild != NULL && pChild->get_value_as_uint32("index", index))
                        {
                            m_backtraceToJsonMap.insert(index, pChild);
                        }
                    }
                }
            }
        }

        if (bBacktraceVisible)
        {
            if (ui->bottomTabWidget->indexOf(ui->callStackTab) == -1)
            {
                // unhide the tab
                ui->bottomTabWidget->insertTab(1, ui->callStackTab, "Call Stack");
                VOGLEDITOR_ENABLE_BOTTOM_TAB(ui->callStackTab);
            }
            else
            {
                VOGLEDITOR_ENABLE_BOTTOM_TAB(ui->callStackTab);
            }
        }
        else
        {
            ui->bottomTabWidget->removeTab(ui->bottomTabWidget->indexOf(ui->callStackTab));
        }

        // machine info
        displayMachineInfo();
   }

   ui->tabWidget->setCurrentWidget(ui->framebufferTab);

   // update toolbar
   m_pPlayButton->setEnabled(true);
   m_pTrimButton->setEnabled(true);

   // timeline
   m_pTimelineModel = new vogleditor_apiCallTimelineModel(m_pApiCallTreeModel->root());
   m_timeline->setModel(m_pTimelineModel);
   m_timeline->repaint();

   m_openFilename = filename.c_str();
   setWindowTitle(m_openFilename + " - " + g_PROJECT_NAME);
   vogleditor_output_message("...opened successfully!");

   this->setCursor(origCursor);
   return true;
}

void VoglEditor::displayMachineInfoHelper(QString prefix, const QString& sectionKeyStr, const vogl::json_value& value, QString& rMachineInfoStr)
{
    if (value.is_array())
    {
        const json_node* pNode = value.get_node_ptr();
        for (uint element = 0; element < pNode->size(); element++)
        {
            dynamic_string elementStr = pNode->get_value(element).as_string();

            elementStr = elementStr.replace("\n", "\n\t");

            rMachineInfoStr += "\t";
            rMachineInfoStr += elementStr.get_ptr();
            rMachineInfoStr += "\n";
        }

        rMachineInfoStr += "\n";
    }
    else if (value.is_node())
    {
        // Check if this is the modoule list.
        bool is_module_list = (sectionKeyStr == "module_list");
        const json_node* pNode = value.get_node_ptr();

        for (uint i = 0; i < pNode->size(); i++)
        {
            dynamic_string key = pNode->get_key(i);
            const json_value &value2 = pNode->get_value(i);

            rMachineInfoStr += prefix;
            // If it's the module list, then the key is the filename and we want to display that last.
            if (!is_module_list)
                rMachineInfoStr += key.c_str();

            if (value2.is_array())
            {
                const json_node* pNode2 = value2.get_node_ptr();

                // If this it module_list, then we get these items: base address, address size, uuid
                // Check in btrace_get_machine_info() to see what's written there.
                for (uint element = 0; element < pNode2->size(); element++)
                {
                    const json_value &json_val = pNode2->get_value(element);

                    if (json_val.is_string())
                    {
                        dynamic_string str = pNode2->get_value(element).as_string();
                        rMachineInfoStr += str.c_str();
                    }
                    else
                    {
                        dynamic_string buf;
                        buf.format("%" PRIx64, json_val.as_uint64());
                        rMachineInfoStr += buf.c_str();
                    }

                    rMachineInfoStr += "\t";
                }
            }
            else
            {
                rMachineInfoStr += ": ";
                rMachineInfoStr += value2.as_string_ptr();
            }

            // Display the filename if this is the module_list.
            if (is_module_list)
                rMachineInfoStr += key.c_str();
            rMachineInfoStr += "\n";
        }

        rMachineInfoStr += "\n";
    }
    else if (value.is_string())
    {
        rMachineInfoStr += value.as_string_ptr();
    }
    else
    {
        rMachineInfoStr += value.as_string_ptr();
    }
}

void VoglEditor::displayMachineInfo()
{
    VOGL_ASSERT(m_pTraceReader != NULL);
    if (m_pTraceReader == NULL)
    {
        return;
    }

    bool bMachineInfoVisible = false;
    if (m_pTraceReader->get_archive_blob_manager().does_exist(VOGL_TRACE_ARCHIVE_MACHINE_INFO_FILENAME))
    {
        uint8_vec machine_info_data;
        if (m_pTraceReader->get_archive_blob_manager().get(VOGL_TRACE_ARCHIVE_MACHINE_INFO_FILENAME, machine_info_data))
        {
            bMachineInfoVisible = true;
            json_document doc;
            json_node *pRoot = doc.get_root();
            if (doc.deserialize((const char*)machine_info_data.get_ptr(), machine_info_data.size()))
            {
                QString text;
                for (uint i = 0; i < pRoot->size(); i++)
                {
                    dynamic_string sectionKeyStr = pRoot->get_key(i);
                    text += pRoot->get_key(i).c_str();
                    text += "\n";

                    QString keyStr = sectionKeyStr.c_str();
                    displayMachineInfoHelper("\t", keyStr, pRoot->get_value(i), text);
                }

                ui->machineInfoText->setText(text);
            }
        }
    }

    if (bMachineInfoVisible)
    {
        if (ui->bottomTabWidget->indexOf(ui->machineInfoTab) == -1)
        {
            // unhide the tab
            ui->bottomTabWidget->insertTab(1, ui->machineInfoTab, "Machine Info");
            VOGLEDITOR_ENABLE_BOTTOM_TAB(ui->machineInfoTab);
        }
        else
        {
            VOGLEDITOR_ENABLE_BOTTOM_TAB(ui->machineInfoTab);
        }
    }
    else
    {
        ui->bottomTabWidget->removeTab(ui->bottomTabWidget->indexOf(ui->machineInfoTab));
    }
}

void VoglEditor::reset_tracefile_ui()
{
    ui->action_Close->setEnabled(false);
    ui->actionExport_API_Calls->setEnabled(false);
    ui->actionSave_Session->setEnabled(false);

    ui->prevSnapshotButton->setEnabled(false);
    ui->nextSnapshotButton->setEnabled(false);
    ui->prevDrawcallButton->setEnabled(false);
    ui->nextDrawcallButton->setEnabled(false);
    ui->searchTextBox->clear();
    ui->searchTextBox->setEnabled(false);
    ui->searchPrevButton->setEnabled(false);
    ui->searchNextButton->setEnabled(false);

    m_pPlayButton->setEnabled(false);
    m_pTrimButton->setEnabled(false);

    VOGLEDITOR_DISABLE_BOTTOM_TAB(ui->machineInfoTab);
    VOGLEDITOR_DISABLE_BOTTOM_TAB(ui->callStackTab);

    reset_snapshot_ui();
}

void VoglEditor::reset_snapshot_ui()
{
    m_currentSnapshot = NULL;

    m_pFramebufferExplorer->clear();
    m_pTextureExplorer->clear();
    m_pRenderbufferExplorer->clear();
    m_pProgramArbExplorer->clear();
    m_pProgramExplorer->clear();
    m_pShaderExplorer->clear();
    m_pBufferExplorer->clear();
    ui->contextComboBox->clear();
    ui->contextComboBox->setEnabled(false);

    ui->stateTreeView->setModel(NULL);

    QWidget* pCurrentTab = ui->tabWidget->currentWidget();

    VOGLEDITOR_DISABLE_STATE_TAB(ui->stateTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->bufferTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->framebufferTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->programArbTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->programTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->shaderTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->textureTab);
    VOGLEDITOR_DISABLE_STATE_TAB(ui->renderbufferTab);

    ui->tabWidget->setCurrentWidget(pCurrentTab);
}

/// This helper will most often return a pointer equal to the pCurSnapshot that is passed in, or NULL if the node does not have a snapshot
/// and also has no children. The pMostRecentSnapshot parameter will be updated to point to the desired snapshot.
/// This function does not follow a traditional DFS search because we need to find the desired snapshot then return the one before it.
/// An alternative approach would be to keep a stack of the found snapshots, or even to build up that stack / list as the user
/// generates new snapshots.
vogleditor_gl_state_snapshot* VoglEditor::findMostRecentSnapshot_helper(vogleditor_apiCallTreeItem* pItem, vogleditor_gl_state_snapshot*& pMostRecentSnapshot, const vogleditor_gl_state_snapshot* pCurSnapshot)
{
    // check if this item has a snapshot shot
    if (pItem->has_snapshot() && pItem->get_snapshot()->is_valid())
    {
        vogleditor_gl_state_snapshot* pTmp = pItem->get_snapshot();
        if (pTmp == pCurSnapshot)
        {
            // if we've reached the item with the current snapshot, we want to return the previous snapshot.
            return pTmp;
        }
        else
        {
            // update most recent snapshot
            pMostRecentSnapshot = pTmp;
        }
    }

    for (int i = 0; i < pItem->childCount(); i++)
    {
        vogleditor_gl_state_snapshot* pTmp = findMostRecentSnapshot_helper(pItem->child(i), pMostRecentSnapshot, pCurSnapshot);
        if (pTmp != NULL)
        {
            if (pTmp == pCurSnapshot)
            {
                // if we've reached the item with the current snapshot, we want to return the previous snapshot.
                return pTmp;
            }
            else
            {
                // update most recent snapshot
                pMostRecentSnapshot = pTmp;
            }
        }
    }

    return NULL;
}

/// This function exists just to simplify the interaction with the helper, so that there no confusion between
/// whether the returned value, or passed in reference parameter should be used as the most recent snapshot.
/// It will either return NULL if there is no recent snapshot (which should only happen for the very first snapshot
/// in a trace), or a pointer to a valid snapshot.
vogleditor_gl_state_snapshot* VoglEditor::findMostRecentSnapshot(vogleditor_apiCallTreeItem* pItem, const vogleditor_gl_state_snapshot* pCurSnapshot)
{
    vogleditor_gl_state_snapshot* pMostRecentSnapshot = NULL;
    findMostRecentSnapshot_helper(pItem, pMostRecentSnapshot, pCurSnapshot);
    return pMostRecentSnapshot;
}

void VoglEditor::update_ui_for_snapshot(vogleditor_gl_state_snapshot* pStateSnapshot)
{
   if (pStateSnapshot == NULL)
   {
      reset_snapshot_ui();
      return;
   }

   if (pStateSnapshot->is_valid() == false)
   {
       reset_snapshot_ui();
       return;
   }

   if (m_currentSnapshot == pStateSnapshot)
   {
       // no need to update if it is the same snapshot
       return;
   }

   m_currentSnapshot = pStateSnapshot;

   if (ui->stateTreeView->model() != NULL)
   {
      if (static_cast<vogleditor_QStateTreeModel*>(ui->stateTreeView->model())->get_snapshot() == m_currentSnapshot)
      {
         // displaying the same snapshot, return
         return;
      }
   }

   QCursor origCursor = this->cursor();
   this->setCursor(Qt::WaitCursor);

   const vogl_context_snapshot_ptr_vec contexts = pStateSnapshot->get_contexts();
   if (contexts.size() > 0)
   {
       vogl_trace_ptr_value currentContextHandle = pStateSnapshot->get_cur_trace_context();

       ui->contextComboBox->clear();
       m_bDelayUpdateUIForContext = true;
       uint indexOfCurrentContext = 0;
       for (uint i = 0; i < contexts.size(); i++)
       {
           vogl_context_snapshot* pContext = contexts[i];
           vogl_trace_ptr_value contextHandle = pContext->get_context_desc().get_trace_context();
           vogl_trace_ptr_value sharedContextHandle = pContext->get_context_desc().get_trace_share_context();

           bool bIsCurrent = contextHandle == currentContextHandle;
           QString additionalInfo;
           if (bIsCurrent)
           {
               indexOfCurrentContext = i;
               additionalInfo = " - (current)";
           }

           bool bSharesAContext = pContext->get_context_desc().get_trace_share_context() != 0;
           if (bSharesAContext)
           {
               QString tmp;
               additionalInfo.append(tmp.sprintf(" - shares context %p", (void*)sharedContextHandle));

               // loop and add nested shared contexts
               sharedContextHandle = pStateSnapshot->get_context(sharedContextHandle)->get_context_desc().get_trace_share_context();
               while (sharedContextHandle != 0)
               {
                   additionalInfo.append(tmp.sprintf(", %p", (void*)sharedContextHandle));
                   sharedContextHandle = pStateSnapshot->get_context(sharedContextHandle)->get_context_desc().get_trace_share_context();
               }
           }

           QString contextTitle;
           contextTitle.sprintf("Context %p%s", (void*)contextHandle, additionalInfo.toStdString().c_str());
           ui->contextComboBox->addItem(contextTitle, QVariant::fromValue(contextHandle));
       }

       m_bDelayUpdateUIForContext = false;
       ui->contextComboBox->setEnabled(true);
       ui->contextComboBox->setCurrentIndex(-1);
       ui->contextComboBox->setCurrentIndex(indexOfCurrentContext);
   }
   else
   {
       ui->contextComboBox->setEnabled(false);
   }

   this->setCursor(origCursor);
}

void VoglEditor::update_ui_for_context(vogl_context_snapshot* pContext, vogleditor_gl_state_snapshot* pStateSnapshot)
{
    if (m_bDelayUpdateUIForContext)
    {
        // update should not be processed right now
        return;
    }

    vogleditor_gl_state_snapshot* pBaseSnapshot = findMostRecentSnapshot(m_pApiCallTreeModel->root(), m_currentSnapshot);

    // state viewer
    if (m_pStateTreeModel != NULL)
    {
        delete m_pStateTreeModel;
    }
    m_pStateTreeModel = new vogleditor_QStateTreeModel(pStateSnapshot, pContext, pBaseSnapshot, NULL);

    ui->stateTreeView->setModel(m_pStateTreeModel);
    ui->stateTreeView->expandToDepth(0);
    ui->stateTreeView->setColumnWidth(0, ui->stateTreeView->width() * 0.5);

    VOGLEDITOR_ENABLE_STATE_TAB(ui->stateTab);

    // vogl stores all the created objects in the deepest context, so need to find that context to populate the UI
    vogl::vector<vogl_context_snapshot*> sharingContexts;
    sharingContexts.push_back(pContext);
    vogl_context_snapshot* pRootContext = pContext;
    vogl_context_snapshot* pTmpContext = NULL;
    while (pRootContext->get_context_desc().get_trace_share_context() != 0)
    {
        pTmpContext = pStateSnapshot->get_context(pRootContext->get_context_desc().get_trace_share_context());
        VOGL_ASSERT(pTmpContext != NULL);
        if (pTmpContext == NULL)
        {
            // this is a bug
            break;
        }

        // update the root context
        pRootContext = pTmpContext;
    }

    // add the root context if it is new (ie, not equal the supplied context)
    if (pRootContext != pContext)
    {
        sharingContexts.push_back(pRootContext);
    }

    // textures
    m_pTextureExplorer->clear();
    uint textureCount = m_pTextureExplorer->set_texture_objects(sharingContexts);

    GLuint curActiveTextureUnit = pContext->get_general_state().get_value<GLuint>(GL_ACTIVE_TEXTURE);
    if (curActiveTextureUnit >= GL_TEXTURE0 && curActiveTextureUnit < (GL_TEXTURE0 + pContext->get_context_info().get_max_texture_image_units()))
    {
        GLuint cur2DBinding = pContext->get_general_state().get_value<GLuint>(GL_TEXTURE_2D_BINDING_EXT, curActiveTextureUnit - GL_TEXTURE0);
        displayTexture(cur2DBinding, false);
    }
    if (textureCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->textureTab); }

    // renderbuffers
    m_pRenderbufferExplorer->clear();
    int renderbufferCount = m_pRenderbufferExplorer->set_renderbuffer_objects(sharingContexts);
    if (renderbufferCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->renderbufferTab); }

    // framebuffer
    m_pFramebufferExplorer->clear();
    uint framebufferCount = m_pFramebufferExplorer->set_framebuffer_objects(pContext, sharingContexts, &(pStateSnapshot->get_default_framebuffer()));
    GLuint64 curDrawFramebuffer = pContext->get_general_state().get_value<GLuint64>(GL_DRAW_FRAMEBUFFER_BINDING);
    displayFramebuffer(curDrawFramebuffer, false);
    if (framebufferCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->framebufferTab); }

    // arb programs
    m_pProgramArbExplorer->clear();
    uint programArbCount = m_pProgramArbExplorer->set_program_objects(sharingContexts);
    GLuint64 curProgramArb = 0; // TODO: pContext->get_general_state().get_value<GLuint64>(GL_CURRENT_PROGRAM);
    m_pProgramArbExplorer->set_active_program(curProgramArb);
    if (programArbCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->programArbTab); }

    // programs
    m_pProgramExplorer->clear();
    uint programCount = m_pProgramExplorer->set_program_objects(sharingContexts);
    GLuint64 curProgram = pContext->get_general_state().get_value<GLuint64>(GL_CURRENT_PROGRAM);
    m_pProgramExplorer->set_active_program(curProgram);
    if (programCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->programTab); }

    // shaders
    m_pShaderExplorer->clear();
    uint shaderCount = m_pShaderExplorer->set_shader_objects(sharingContexts);
    if (curProgram != 0)
    {
        bool bFound = false;
        for (uint c = 0; c < sharingContexts.size(); c++)
        {
            vogl_gl_object_state_ptr_vec programObjects;
            sharingContexts[c]->get_all_objects_of_category(cGLSTProgram, programObjects);
            for (vogl_gl_object_state_ptr_vec::iterator iter = programObjects.begin(); iter != programObjects.end(); iter++)
            {
                if ((*iter)->get_snapshot_handle() == curProgram)
                {
                    vogl_program_state* pProgramState = static_cast<vogl_program_state*>(*iter);
                    if (pProgramState->get_attached_shaders().size() > 0)
                    {
                        uint curShader = pProgramState->get_attached_shaders()[0];
                        m_pShaderExplorer->set_active_shader(curShader);
                    }

                    bFound = true;
                    break;
                }
            }

            if (bFound)
                break;
        }
    }
    if (shaderCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->shaderTab); }

    // buffers
    m_pBufferExplorer->clear();
    uint bufferCount = m_pBufferExplorer->set_buffer_objects(sharingContexts);
    if (bufferCount > 0) { VOGLEDITOR_ENABLE_STATE_TAB(ui->bufferTab); }

}

void VoglEditor::on_stateTreeView_clicked(const QModelIndex &index)
{
   vogleditor_stateTreeItem* pStateItem = static_cast<vogleditor_stateTreeItem*>(index.internalPointer());
   if (pStateItem == NULL)
   {
      return;
   }

   switch(pStateItem->getStateType())
   {
   case vogleditor_stateTreeItem::cTEXTURE:
   {
      vogleditor_stateTreeTextureItem* pTextureItem = static_cast<vogleditor_stateTreeTextureItem*>(pStateItem);
      if (pTextureItem == NULL)
      {
         break;
      }

      displayTexture(pTextureItem->get_texture_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cPROGRAM:
   {
      vogleditor_stateTreeProgramItem* pProgramItem = static_cast<vogleditor_stateTreeProgramItem*>(pStateItem);
      if (pProgramItem == NULL)
      {
         break;
      }

      displayProgram(pProgramItem->get_current_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cPROGRAMARB:
   {
      vogleditor_stateTreeArbProgramItem* pProgramArbItem = static_cast<vogleditor_stateTreeArbProgramItem*>(pStateItem);
      if (pProgramArbItem == NULL)
      {
         break;
      }

      displayProgramArb(pProgramArbItem->get_current_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cSHADER:
   {
      vogleditor_stateTreeShaderItem* pShaderItem = static_cast<vogleditor_stateTreeShaderItem*>(pStateItem);
      if (pShaderItem == NULL)
      {
         break;
      }

      displayShader(pShaderItem->get_current_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cFRAMEBUFFER:
   {
      vogleditor_stateTreeFramebufferItem* pFramebufferItem = static_cast<vogleditor_stateTreeFramebufferItem*>(pStateItem);
      if (pFramebufferItem == NULL)
      {
         break;
      }

      displayFramebuffer(pFramebufferItem->get_framebuffer_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cBUFFER:
   {
      vogleditor_stateTreeBufferItem* pBufferItem = static_cast<vogleditor_stateTreeBufferItem*>(pStateItem);
      if (pBufferItem == NULL)
      {
         break;
      }

      displayBuffer(pBufferItem->get_buffer_state()->get_snapshot_handle(), true);

      break;
   }
   case vogleditor_stateTreeItem::cDEFAULT:
   {
      return;
   }
   }
}

bool VoglEditor::displayShader(GLuint64 shaderHandle, bool bBringTabToFront)
{
    bool bDisplayed = false;
    if (m_pShaderExplorer->set_active_shader(shaderHandle))
    {
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->shaderTab);
        }
    }

    return bDisplayed;
}

bool VoglEditor::displayBuffer(GLuint64 bufferHandle, bool bBringTabToFront)
{
    bool bDisplayed = false;
    if (m_pBufferExplorer->set_active_buffer(bufferHandle))
    {
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->bufferTab);
        }
    }

    return bDisplayed;
}

void VoglEditor::displayProgramArb(GLuint64 programArbHandle, bool bBringTabToFront)
{
    if (m_pProgramArbExplorer->set_active_program(programArbHandle))
    {
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->programArbTab);
        }
    }
}

void VoglEditor::displayProgram(GLuint64 programHandle, bool bBringTabToFront)
{
    if (m_pProgramExplorer->set_active_program(programHandle))
    {
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->programTab);
        }
    }
}

void VoglEditor::displayFramebuffer(GLuint64 framebufferHandle, bool bBringTabToFront)
{
    bool bDisplayedFBO = m_pFramebufferExplorer->set_active_framebuffer(framebufferHandle);

    if (bDisplayedFBO)
    {
        VOGLEDITOR_ENABLE_STATE_TAB(ui->framebufferTab);
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->framebufferTab);
        }
    }
}

bool VoglEditor::displayTexture(GLuint64 textureHandle, bool bBringTabToFront)
{
    bool bDisplayedTexture = m_pTextureExplorer->set_active_texture(textureHandle);

    if (bDisplayedTexture)
    {
        VOGLEDITOR_ENABLE_STATE_TAB(ui->textureTab);
        if (bBringTabToFront)
        {
            ui->tabWidget->setCurrentWidget(ui->textureTab);
        }
    }

    return bDisplayedTexture;
}

void VoglEditor::slot_treeView_currentChanged(const QModelIndex & current, const QModelIndex & previous)
{
    VOGL_NOTE_UNUSED(previous);
    onApiCallSelected(current, false);
}

void VoglEditor::on_treeView_clicked(const QModelIndex &index)
{
    onApiCallSelected(index, true);
}

void VoglEditor::onApiCallSelected(const QModelIndex &index, bool bAllowStateSnapshot)
{
    vogleditor_apiCallTreeItem* pCallTreeItem = static_cast<vogleditor_apiCallTreeItem*>(index.internalPointer());
    if (pCallTreeItem == NULL)
    {
       return;
    }

    vogleditor_frameItem* pFrameItem = pCallTreeItem->frameItem();
    vogleditor_apiCallItem* pApiCallItem = pCallTreeItem->apiCallItem();

    if (bAllowStateSnapshot && pCallTreeItem == m_pCurrentCallTreeItem)
    {
        // we can only get snapshots for specific API calls
        if (pApiCallItem != NULL && pApiCallItem->needs_snapshot())
        {
           // get the snapshot after the current api call
           vogleditor_gl_state_snapshot* pNewSnapshot = NULL;
           QCursor origCursor = cursor();
           setCursor(Qt::WaitCursor);
           m_traceReplayer.replay(m_pTraceReader, m_pApiCallTreeModel->root(), &pNewSnapshot, pApiCallItem->globalCallIndex(), false);
           setCursor(origCursor);
           pCallTreeItem->set_snapshot(pNewSnapshot);
        }
    }

    update_ui_for_snapshot(pCallTreeItem->get_snapshot());

    if (pApiCallItem != NULL && m_pCurrentCallTreeItem != pCallTreeItem)
    {
        if (m_backtraceToJsonMap.size() > 0)
        {
            QString tmp;
            json_node* pBacktraceNode = m_backtraceToJsonMap[(uint)pApiCallItem->backtraceHashIndex()];
            if (pBacktraceNode != NULL)
            {
                json_node* pAddrs = pBacktraceNode->find_child_array("addrs");
                json_node* pSyms = pBacktraceNode->find_child_array("syms");

                for (uint i = 0; i < pAddrs->size(); i++)
                {
                    tmp += pAddrs->get_value(i).as_string_ptr();
                    if (pSyms)
                    {
                        tmp += "\t";
                        tmp += pSyms->get_value(i).as_string_ptr();
                    }
                    tmp += "\n";
                }
            }
            ui->backtraceText->setText(tmp);
        }
    }

    if (pApiCallItem != NULL)
    {
        m_timeline->setCurrentApiCall(pApiCallItem->globalCallIndex());
    }

    if (pFrameItem != NULL)
    {
       m_timeline->setCurrentFrame(pFrameItem->frameNumber());
    }

    m_timeline->repaint();

    m_pCurrentCallTreeItem = pCallTreeItem;
}

void VoglEditor::selectApicallModelIndex(QModelIndex index, bool scrollTo, bool select)
{
    // make sure the index is visible
    QModelIndex parentIndex = index.parent();
    while (parentIndex.isValid())
    {
        if (ui->treeView->isExpanded(parentIndex) == false)
        {
            ui->treeView->expand(parentIndex);
        }
        parentIndex = parentIndex.parent();
    }

    // scroll to the index
    if (scrollTo)
    {
        ui->treeView->scrollTo(index);
    }

    // select the index
    if (select)
    {
        ui->treeView->setCurrentIndex(index);
    }
}

void VoglEditor::on_searchTextBox_textChanged(const QString &searchText)
{
    QPalette palette(ui->searchTextBox->palette());
    palette.setColor(QPalette::Base, m_searchTextboxBackgroundColor);
    ui->searchTextBox->setPalette(palette);

    if (m_pApiCallTreeModel != NULL)
    {
        m_pApiCallTreeModel->set_highlight_search_string(searchText);
    }

    // need to briefly give the treeview focus so that it properly redraws and highlights the matching rows
    // then return focus to the search textbox so that typed keys are not lost
    ui->treeView->setFocus();
    ui->searchTextBox->setFocus();
}

void VoglEditor::on_searchNextButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        QModelIndex index = m_pApiCallTreeModel->find_next_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
        if (index.isValid())
        {
            selectApicallModelIndex(index, true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::on_searchPrevButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        QModelIndex index = m_pApiCallTreeModel->find_prev_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
        if (index.isValid())
        {
            selectApicallModelIndex(index, true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::on_prevSnapshotButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        vogleditor_apiCallTreeItem* pPrevItemWithSnapshot = m_pApiCallTreeModel->find_prev_snapshot(m_pCurrentCallTreeItem);
        if (pPrevItemWithSnapshot != NULL)
        {
            selectApicallModelIndex(m_pApiCallTreeModel->indexOf(pPrevItemWithSnapshot), true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::on_nextSnapshotButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        vogleditor_apiCallTreeItem* pNextItemWithSnapshot = m_pApiCallTreeModel->find_next_snapshot(m_pCurrentCallTreeItem);
        if (pNextItemWithSnapshot != NULL)
        {
            selectApicallModelIndex(m_pApiCallTreeModel->indexOf(pNextItemWithSnapshot), true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::on_prevDrawcallButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        vogleditor_apiCallTreeItem* pPrevItem = m_pApiCallTreeModel->find_prev_drawcall(m_pCurrentCallTreeItem);
        if (pPrevItem != NULL)
        {
            selectApicallModelIndex(m_pApiCallTreeModel->indexOf(pPrevItem), true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::on_nextDrawcallButton_clicked()
{
    if (m_pApiCallTreeModel != NULL)
    {
        vogleditor_apiCallTreeItem* pNextItem = m_pApiCallTreeModel->find_next_drawcall(m_pCurrentCallTreeItem);
        if (pNextItem != NULL)
        {
            selectApicallModelIndex(m_pApiCallTreeModel->indexOf(pNextItem), true, true);
            ui->treeView->setFocus();
        }
    }
}

void VoglEditor::slot_program_edited(vogl_arb_program_state* pNewProgramState)
{
    VOGL_NOTE_UNUSED(pNewProgramState);

    m_currentSnapshot->set_edited(true);

    // update all the snapshot flags
    bool bFoundEditedSnapshot = false;
    recursive_update_snapshot_flags(m_pApiCallTreeModel->root(), bFoundEditedSnapshot);

    // give the tree view focus so that it redraws. This is something of a hack, we don't really want to be changing around which control has focus,
    // but right now I don't see it being a major issue. It may be an issue later on depending on how we implement more state editing (ie, if arrow
    // keys are used to cycle through options in a drop-down, and the tree view gets focus, the arrow keys would then start changing the selected
    // API call instead of cycling through state options).
    ui->treeView->setFocus();
}

void VoglEditor::slot_program_edited(vogl_program_state* pNewProgramState)
{
    VOGL_NOTE_UNUSED(pNewProgramState);

    m_currentSnapshot->set_edited(true);

    // update all the snapshot flags
    bool bFoundEditedSnapshot = false;
    recursive_update_snapshot_flags(m_pApiCallTreeModel->root(), bFoundEditedSnapshot);

    // give the tree view focus so that it redraws. This is something of a hack, we don't really want to be changing around which control has focus,
    // but right now I don't see it being a major issue. It may be an issue later on depending on how we implement more state editing (ie, if arrow
    // keys are used to cycle through options in a drop-down, and the tree view gets focus, the arrow keys would then start changing the selected
    // API call instead of cycling through state options).
    ui->treeView->setFocus();
}

// if an edited snapshot has already been found, mark the node (and all children) as dirty.
void VoglEditor::recursive_update_snapshot_flags(vogleditor_apiCallTreeItem* pItem, bool& bFoundEditedSnapshot)
{
    // check if this item has a snapshot shot
    if (pItem->has_snapshot())
    {
        if (!bFoundEditedSnapshot)
        {
            if (pItem->get_snapshot()->is_edited())
            {
                bFoundEditedSnapshot = true;
            }
            else
            {
                pItem->get_snapshot()->set_outdated(false);
            }
        }
        else
        {
            pItem->get_snapshot()->set_outdated(true);
        }
    }

    for (int i = 0; i < pItem->childCount(); i++)
    {
        recursive_update_snapshot_flags(pItem->child(i), bFoundEditedSnapshot);
    }
}

#undef VOGLEDITOR_DISABLE_STATE_TAB
#undef VOGLEDITOR_ENABLE_STATE_TAB

#undef VOGLEDITOR_DISABLE_BOTTOM_TAB
#undef VOGLEDITOR_ENABLE_BOTTOM_TAB

void VoglEditor::on_actionSave_Session_triggered()
{
    QString baseName = m_openFilename;

    int lastIndex = baseName.lastIndexOf('.');
    if (lastIndex != -1)
    {
        baseName = baseName.remove(lastIndex, baseName.size() - lastIndex);
    }

    QString suggestedName = baseName + "-vogleditor.json";

    QString sessionFilename = QFileDialog::getSaveFileName(this, tr("Save Debug Session"), suggestedName, tr("JSON (*.json)"));

    if (!save_session_to_disk(sessionFilename))
    {
        vogleditor_output_error("Failed to save session.");
    }
}

void VoglEditor::on_actionOpen_Session_triggered()
{
    QString sessionFilename = QFileDialog::getOpenFileName(this, tr("Load Debug Session"), QString(), tr("JSON (*.json)"));

    QCursor origCursor = this->cursor();
    setCursor(Qt::WaitCursor);

    if (!load_session_from_disk(sessionFilename))
    {
        vogleditor_output_error("Failed to load session.");
    }

    setCursor(origCursor);
}

void VoglEditor::on_searchTextBox_returnPressed()
{
    if (m_pApiCallTreeModel != NULL)
    {
        QModelIndex index = m_pApiCallTreeModel->find_next_search_result(m_pCurrentCallTreeItem, ui->searchTextBox->text());
        if (index.isValid())
        {
            // a valid item was found, scroll to it and select it
            selectApicallModelIndex(index, true, true);
        }
        else
        {
            // no items were found, so set the textbox background to red (it will get cleared to the original color if the user edits the search text)
            QPalette palette(ui->searchTextBox->palette());
            palette.setColor(QPalette::Base, Qt::red);
            ui->searchTextBox->setPalette(palette);
        }
    }
}

void VoglEditor::slot_readReplayStandardOutput()
{
    m_pVoglReplayProcess->setReadChannel(QProcess::StandardOutput);
    while (m_pVoglReplayProcess->canReadLine())
    {
        QByteArray output = m_pVoglReplayProcess->readLine();
        if (output.endsWith("\n"))
        {
            output.remove(output.size() - 1, 1);
        }
        vogleditor_output_message(output.constData());
    }
}

void VoglEditor::slot_readReplayStandardError()
{
    m_pVoglReplayProcess->setReadChannel(QProcess::StandardError);
    while (m_pVoglReplayProcess->canReadLine())
    {
        QByteArray output = m_pVoglReplayProcess->readLine();
        if (output.endsWith("\n"))
        {
            output.remove(output.size() - 1, 1);
        }
        vogleditor_output_error(output.constData());
    }
}

void VoglEditor::on_contextComboBox_currentIndexChanged(int index)
{
    if (m_currentSnapshot != NULL)
    {
        vogl_trace_ptr_value contextHandle = ui->contextComboBox->itemData(index).value<vogl_trace_ptr_value>();
        if (contextHandle != 0)
        {
            vogl_context_snapshot* pContext = m_currentSnapshot->get_context(contextHandle);
            update_ui_for_context(pContext, m_currentSnapshot);
        }
    }
}
