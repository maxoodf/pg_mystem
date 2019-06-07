#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/mman.h>

#include <string>
#include <vector>
#include <random>
#include <limits>
#include <algorithm>

#include "rapidjson/document.h"

extern "C" {
    #include <postgres.h>
    #include <miscadmin.h>
    #include <postmaster/bgworker.h>
    #include <storage/ipc.h>
    #include <storage/latch.h>
    #include <fmgr.h>
    #include <utils/builtins.h>

    #if PG_VERSION_NUM >= 100000
    #include <pgstat.h>
    #endif
    
    PG_MODULE_MAGIC;
}

namespace pg_ms {
#ifndef DOC_LEN_MAX
#error "DOC_LEN_MAX must be defined"
#else
    static const uint32_t docLengthMax = DOC_LEN_MAX;
#endif
    
#ifndef MYSTEM_PROCS
#error "MYSTEM_PROCS must be defined"
#else
    static const uint8_t mystemProcNo = MYSTEM_PROCS;
#endif
    
    static const std::string mystemParagraphEndMarker = "EndOfArticleMarker";
    
    // document[DOC_LEN_MAX] + ' ' + "EndOfArticleMarker" + '\n' + '\0'
    static const uint32_t docAndPostfixLengthMax = docLengthMax + 21 * sizeof(char);
    
    static const useconds_t queueWaitTimeoutMax = 1000L; // wait for a record, milliseconds

    #if PG_VERSION_NUM >= 100000
    static const char *moduleName = "pg_mystem";
    static const int latchEventInfo = WAIT_EVENT_BGWRITER_HIBERNATE;
    #endif

    typedef void (*bgwMain_t)(Datum main_arg);

    static int WaitLatch(volatile Latch *latch, int wakeEvents, long timeout) {
    #if PG_VERSION_NUM < 100000
        return ::WaitLatch(latch, wakeEvents, timeout);
    #else
        return ::WaitLatch(latch, wakeEvents, timeout, latchEventInfo);
    #endif
    }

    static void setBgWorkerMain(BackgroundWorker &worker, bgwMain_t main, const char *mainName) {
    #if PG_VERSION_NUM < 100000
        worker.bgw_main = main;
    #else
        strncpy(worker.bgw_library_name, moduleName, BGW_MAXLEN);
        strncpy(worker.bgw_function_name, mainName, BGW_MAXLEN);
    #endif
    }

    class inOutQueue_t {
    public:
        static const uint16_t queueRecordsMax;
        
        struct inQueueRecord_t {
            uint64_t m_id;
            char m_text[docAndPostfixLengthMax];
        };
        struct outQueueRecord_t {
            uint64_t m_id;
            char m_text[docAndPostfixLengthMax];
        };
        
    private:
        static const char *inQueueSemName;
        static const char *inQueueShmName;
        
        static const char *outQueueSemName;
        static const char *outQueueShmName;
        
        sem_t *m_inQueueSem;
        sem_t *m_outQueueSem;
        
        int m_inQueueShm;
        int m_outQueueShm;
        
        inQueueRecord_t *m_inQueue;
        outQueueRecord_t *m_outQueue;
        
        bool m_OK;
        int m_errCode;
        
    public:
        static bool init() {
            sem_t *inQueueSem = sem_open(inQueueSemName, O_CREAT, 0600, 0);
            if (inQueueSem == SEM_FAILED) {
                elog(LOG, "MYSTEM: inOutQueue(1) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            
            sem_t *outQueueSem = sem_open(outQueueSemName, O_CREAT, 0600, 0);
            if (outQueueSem == SEM_FAILED) {
                elog(LOG, "MYSTEM: inOutQueue(2) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            
            int inQueueShm = shm_open(inQueueShmName, O_CREAT | O_RDWR, 0600);
            if (inQueueShm == -1) {
                elog(LOG, "MYSTEM: inOutQueue(3) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            
            int outQueueShm = shm_open(outQueueShmName, (O_CREAT | O_RDWR), 0600);
            if (outQueueShm == -1) {
                elog(LOG, "MYSTEM: inOutQueue(4) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            
            if (ftruncate(inQueueShm, (off_t) sizeof(inQueueRecord_t) * queueRecordsMax) != 0) {
                elog(LOG, "MYSTEM: inOutQueue(5) init error = %d, %s", errno, strerror(errno));
            }
            
            if (ftruncate(outQueueShm, (off_t) sizeof(outQueueRecord_t) * queueRecordsMax) != 0) {
                elog(LOG, "MYSTEM: inOutQueue(6) init error = %d, %s", errno, strerror(errno));
            }
            
            inQueueRecord_t *inQueue = (inQueueRecord_t *) mmap((void *) 0, sizeof(inQueueRecord_t) * queueRecordsMax,
                                                                PROT_WRITE, MAP_SHARED, inQueueShm, (off_t) 0);
            if (inQueue == MAP_FAILED) {
                elog(LOG, "MYSTEM: inOutQueue(7) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            outQueueRecord_t *outQueue = (outQueueRecord_t *) mmap((void *) 0, sizeof(outQueueRecord_t) * queueRecordsMax,
                                                                   PROT_WRITE, MAP_SHARED, outQueueShm, (off_t) 0);
            if (outQueue == MAP_FAILED) {
                elog(LOG, "MYSTEM: inOutQueue(8) init error = %d, %s", errno, strerror(errno));
                return false;
            }
            
            bzero((void *) inQueue, sizeof(inQueueRecord_t) * queueRecordsMax);
            bzero((void *) outQueue, sizeof(outQueueRecord_t) * queueRecordsMax);
            
            munmap((void *) inQueue, sizeof(inQueueRecord_t) * queueRecordsMax);
            close(inQueueShm);
            sem_post(inQueueSem);
            
            munmap((void *) outQueue, sizeof(outQueueRecord_t) * queueRecordsMax);
            close(outQueueShm);
            sem_post(outQueueSem);
            
            return true;
        }
        
        static void release() {
            sem_unlink(inQueueSemName);
            shm_unlink(inQueueShmName);
            
            sem_unlink(outQueueSemName);
            shm_unlink(outQueueShmName);
        }
        
        inOutQueue_t(): m_inQueueSem(SEM_FAILED), m_outQueueSem(SEM_FAILED), m_inQueueShm(-1), m_outQueueShm(-1),
        m_inQueue(nullptr), m_outQueue(nullptr), m_OK(false), m_errCode(0) {
            m_inQueueSem = sem_open(inQueueSemName, 0);
            if (m_inQueueSem == SEM_FAILED) {
                m_errCode = errno;
                return;
            }
            
            m_outQueueSem = sem_open(outQueueSemName, 0);
            if (m_outQueueSem == SEM_FAILED) {
                m_errCode = errno;
                return;
            }
            
            m_inQueueShm = shm_open(inQueueShmName, O_RDWR, 0600);
            if (m_inQueueShm == -1) {
                m_errCode = errno;
                return;
            }
            
            m_outQueueShm = shm_open(outQueueShmName, O_RDWR, 0600);
            if (m_outQueueShm == -1) {
                m_errCode = errno;
                return;
            }
            
            m_inQueue = (inQueueRecord_t *) mmap((void *) 0, sizeof(inQueueRecord_t) * queueRecordsMax,
                                                 PROT_WRITE, MAP_SHARED, m_inQueueShm, (off_t) 0);
            if (m_inQueue == MAP_FAILED) {
                m_errCode = errno;
                return;
            }
            
            m_outQueue = (outQueueRecord_t *) mmap((void *) 0, sizeof(outQueueRecord_t) * queueRecordsMax,
                                                   PROT_WRITE, MAP_SHARED, m_outQueueShm, (off_t) 0);
            if (m_outQueue == MAP_FAILED) {
                m_errCode = errno;
                return;
            }
            
            m_OK = true;
        }
        
        ~inOutQueue_t() {
            if (m_inQueue != nullptr) {
                munmap(m_inQueue, sizeof(inQueueRecord_t) * queueRecordsMax);
            }
            if (m_inQueueShm > 0) {
                close(m_inQueueShm);
            }
            if (m_inQueueSem != SEM_FAILED) {
                sem_close(m_inQueueSem);
            }
            
            if (m_outQueue != nullptr) {
                munmap(m_outQueue, sizeof(outQueueRecord_t) * queueRecordsMax);
            }
            if (m_outQueueShm > 0) {
                close(m_outQueueShm);
            }
            if (m_outQueueSem != SEM_FAILED) {
                sem_close(m_outQueueSem);
            }
        }
        
        bool isOK() const {
            return m_OK;
        }
        
        int errCode() const {
            return m_errCode;
        }
        
        uint64_t setInQueueRecord(const std::string &_text) {
            uint64_t ret = 0;
            if (sem_trywait(m_inQueueSem) == 0) {
                for (auto i = 0; i < queueRecordsMax; ++i) {
                    if (m_inQueue[i].m_id == 0) {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(1, std::numeric_limits<int>::max());
                        ret = m_inQueue[i].m_id = dis(gen);
                        
                        std::string text = _text;
                        if (text.length() > docLengthMax) {
                            const char delimChars[] = " .,!@#$%^&*()_-+={[}];:'\"~`<>?/\n\t";
                            std::size_t pos = text.find_last_of(delimChars, docLengthMax - 1, 1);
                            if (pos == std::string::npos) {
                                pos = docLengthMax - 1;
                            }
                            text = text.substr(0, pos + 1);
                        }
                        strncpy(m_inQueue[i].m_text,
                                (text + " " + mystemParagraphEndMarker + "\n").c_str(),
                                docAndPostfixLengthMax - 1);
                        break;
                    }
                }
                sem_post(m_inQueueSem);
            }
            
            return ret;
        }
        
        uint64_t getInQueueRecord(std::string &_text) {
            uint64_t ret = 0;
            if (sem_trywait(m_inQueueSem) == 0) {
                for (auto i = 0; i < queueRecordsMax; ++i) {
                    if (m_inQueue[i].m_id != 0) {
                        ret = m_inQueue[i].m_id;
                        m_inQueue[i].m_id = 0;
                        _text = m_inQueue[i].m_text;
                        bzero((void *) m_inQueue[i].m_text, docAndPostfixLengthMax);
                        break;
                    }
                }
                sem_post(m_inQueueSem);
            }
            
            return ret;
        }
        
        bool setOutQueueRecord(uint64_t _id, const std::string &_text) {
            bool ret = false;
            if (sem_trywait(m_outQueueSem) == 0) {
                for (auto i = 0; i < queueRecordsMax; ++i) {
                    if (m_outQueue[i].m_id == 0) {
                        m_outQueue[i].m_id = _id;
                        std::string text = _text;
                        if (text.length() > docAndPostfixLengthMax - 2) {
                            text = text.substr(0, docAndPostfixLengthMax - 2);
                        }
                        strncpy(m_outQueue[i].m_text,
                                (text + "\n").c_str(),
                                docAndPostfixLengthMax - 1);
                        ret = true;
                        break;
                    }
                }
                sem_post(m_outQueueSem);
            }
            
            return ret;
        }
        
        bool getOutQueueRecord(uint64_t _id, std::string &_text) {
            bool ret = false;
            if (sem_trywait(m_outQueueSem) == 0) {
                for (auto i = 0; i < queueRecordsMax; ++i) {
                    if (m_outQueue[i].m_id == _id) {
                        m_outQueue[i].m_id = 0;
                        _text = m_outQueue[i].m_text;
                        bzero((void *) m_outQueue[i].m_text, docAndPostfixLengthMax);
                        ret = true;
                        break;
                    }
                }
                sem_post(m_outQueueSem);
            }
            
            return ret;
        }
        
    private:
        
    };
    
    const uint16_t inOutQueue_t::queueRecordsMax = mystemProcNo * 2;
    const char *inOutQueue_t::inQueueSemName = "/pg_mystemInQueueSem";
    const char *inOutQueue_t::inQueueShmName = "/pg_mystemInQueueShm";
    const char *inOutQueue_t::outQueueSemName = "/pg_mystemOutQueueSem";
    const char *inOutQueue_t::outQueueShmName = "/pg_mystemOutQueueShm";
}

extern "C" {
    static volatile sig_atomic_t mystemTerminated = false;
    
    static void mystemSigterm(SIGNAL_ARGS) {
        int save_errno = errno;
        mystemTerminated = true;
        SetLatch(MyLatch);
        errno = save_errno;
    }
    
    #ifndef SHARE_FOLDER
        #error "SHARE_FOLDER must be defined"
    #else
        #define xstr(s) str(s)
        #define str(s) #s
        #define SHARE_FOLDER_STR xstr(SHARE_FOLDER)
    #endif

    void createMystemChilds(Datum _arg) {
        int stdinPipe[2];
        int stdoutPipe[2];
        
        if ((pipe(stdinPipe) < 0) || (pipe(stdoutPipe) < 0)) {
            elog(ERROR, "MYSTEM: pipe call failed, errno = %d", errno);
            proc_exit(1);
        }
        
        pid_t derivedPid = fork();
        switch (derivedPid) {
            case 0: {
                // child continues here
                close(STDOUT_FILENO);
                close(STDIN_FILENO);
                // redirect stdin, stdout, stderr
                if ((dup2(stdoutPipe[0], STDIN_FILENO) == -1) || (dup2(stdinPipe[1], STDOUT_FILENO) == -1)) {
                    elog(ERROR, "MYSTEM: IO redirection failed, errno = %d", errno);
                    proc_exit(1);
                }
                close(stdoutPipe[0]);
                close(stdoutPipe[1]);
                close(stdinPipe[0]);
                close(stdinPipe[1]);
                
                // run child process image
                std::string path = std::string(SHARE_FOLDER_STR) + "/mystem";
                execle(path.c_str(), path.c_str(), "-cd", "--format", "json", NULL, NULL);
                
                // if we get here at all, an error occurred, but we are in the child
                // process, so just exit
                elog(ERROR, "MYSTEM: exec of the child process failed (%s), errno = %d", path.c_str(), errno);
                proc_exit(1);
            } case -1: {
                // failed to create child
                close(stdoutPipe[0]);
                close(stdoutPipe[1]);
                close(stdinPipe[0]);
                close(stdinPipe[1]);
                elog(ERROR, "MYSTEM: failed to create child, errno=%d", errno);
                proc_exit(1);
            } default: {
                // parent continues here
                close(stdoutPipe[0]);
                close(stdinPipe[1]);

                pqsignal(SIGTERM, mystemSigterm);
                BackgroundWorkerUnblockSignals();

                try {
                    pg_ms::inOutQueue_t inOutQueue;
                    if (!inOutQueue.isOK()) {
                        elog(ERROR, "MYSTEM: failed to attach queue");
                        proc_exit(1);
                    }
                    
                    elog(LOG, "MYSTEM: initialized");
                    
                    long currTimeout = 0;
                    while (!mystemTerminated) {
                        std::string writeLine;
                        uint64_t id = inOutQueue.getInQueueRecord(writeLine);
                        std::string normLine;
                        if (id > 0) {
                            currTimeout = 0;
                            if (writeLine.length() > 0) {
                                std::size_t ttl = 0;
                                while (true) {
                                    ssize_t wrote = write(stdoutPipe[1], writeLine.c_str() + ttl, writeLine.length() - ttl);
                                    if (wrote < 0) {
                                        elog(LOG, "MYSTEM: write to mystem failed");
                                        break;
                                    }
                                    ttl += wrote;
                                    if (wrote == 0 || ttl >= writeLine.length()) {
                                        break;
                                    }
                                }
                                
                                std::vector<std::string> normLines;
                                std::string readLine;
                                char rChar = 0;
                                while (true) {
                                    ssize_t red = read(stdinPipe[0], &rChar, sizeof(rChar));
                                    if (red > 0) {
                                        if (rChar == '\n') {
                                            if (readLine.find(pg_ms::mystemParagraphEndMarker) != std::string::npos) {
                                                normLines.push_back(readLine);
                                                readLine.clear();
                                                break;
                                            } else {
                                                normLines.push_back(readLine);
                                                readLine.clear();
                                            }
                                        } else {
                                            readLine += rChar;
                                        }
                                    } else if (red <= 0) {
                                        break;
                                    }
                                }
                                
                                for (auto nli:normLines) {
                                    rapidjson::Document jsonDoc;
                                    jsonDoc.Parse<0>(nli.c_str());
                                    if (jsonDoc.IsArray()) {
                                        const rapidjson::Value &array = jsonDoc;
                                        for (rapidjson::SizeType i = 0; i < array.Size(); ++i) {
                                            std::string anlsStr;
                                            if (array[i].HasMember("analysis")) {
                                                if (array[i]["analysis"].IsArray()) {
                                                    const rapidjson::Value &analysis = array[i]["analysis"];
                                                    for (rapidjson::SizeType j = 0; j < analysis.Size(); ++j) {
                                                        if (analysis[j].HasMember("lex")) {
                                                            anlsStr = analysis[j]["lex"].GetString();
                                                        } else {
                                                            elog(LOG, "MYSTEM: JSON format error");
                                                            break;
                                                        }
                                                    }
                                                } else {
                                                    elog(LOG, "MYSTEM: JSON format error");
                                                    break;
                                                }
                                            }
                                            if (array[i].HasMember("text")) {
                                                if (anlsStr.length() == 0) {
                                                    std::string text = array[i]["text"].GetString();
                                                    if (text != pg_ms::mystemParagraphEndMarker && text != "\n") {
                                                        std::replace(text.begin(), text.end(), '\n', ' ');
                                                        normLine += text;
                                                    }
                                                } else {
                                                    normLine += anlsStr;
                                                }
                                            } else {
                                                elog(LOG, "MYSTEM: JSON format error");
                                                break;
                                            }
                                        }
                                    } else {
                                        elog(LOG, "MYSTEM: JSON parsing failed");
                                    }
                                }
                            }
                            
                            while (true) {
                                if (inOutQueue.setOutQueueRecord(id, normLine)) {
                                    break;
                                } else {
                                    usleep(1L);
                                }
                            }
                        }
                        
                        currTimeout = (currTimeout + 1) * 2;
                        if (currTimeout > pg_ms::queueWaitTimeoutMax) {
                            currTimeout = pg_ms::queueWaitTimeoutMax;
                        }
                        
                        int rc = pg_ms::WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH, currTimeout);
                        ResetLatch(MyLatch);
                        if (rc & WL_POSTMASTER_DEATH) {
                            break;
                        }
                    }
                } catch (const std::exception &_e) {
                    elog(LOG, "MYSTEM: critical error: %s", _e.what());
                } catch (...) {
                    elog(LOG, "MYSTEM: unknown critical");
                }
                
                elog(LOG, "MYSTEM: worker is shutting down");
                close(stdinPipe[0]);
                close(stdoutPipe[1]);
                proc_exit(0);
            }
        }
    }

    void mainMystemProc(Datum) {
        if (!pg_ms::inOutQueue_t::init()) {
            elog(ERROR, "MYSTEM: queue initialisation failed");
            proc_exit(1);
        }
        
        BackgroundWorker worker;
        worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
        worker.bgw_start_time = BgWorkerStart_ConsistentState;
        worker.bgw_restart_time = BGW_NEVER_RESTART;
        pg_ms::setBgWorkerMain(worker, createMystemChilds, "createMystemChilds");

        for (auto i = 0; i < pg_ms::mystemProcNo; ++i) {
            sprintf(worker.bgw_name, "mystem wrapper process %d", i + 1);
            RegisterDynamicBackgroundWorker(&worker, NULL);
        }

        pqsignal(SIGTERM, mystemSigterm);
        BackgroundWorkerUnblockSignals();

        while (!mystemTerminated) {
            int rc = pg_ms::WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH, 1000L);
            ResetLatch(MyLatch);
            if (rc & WL_POSTMASTER_DEATH) {
                break;
            }
        }

        elog(LOG, "MYSTEM: launcher is shutting down");
        
        pg_ms::inOutQueue_t::release();
        proc_exit(0);
    }
    
    void _PG_init(void) {
        BackgroundWorker worker;
        
        sprintf(worker.bgw_name, "mystem wrapper launcher process");
        worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
        worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
        worker.bgw_restart_time = BGW_NEVER_RESTART;
        worker.bgw_notify_pid = 0;
        pg_ms::setBgWorkerMain(worker, mainMystemProc, "mainMystemProc");
        
        RegisterBackgroundWorker(&worker);
    }
    
    PG_FUNCTION_INFO_V1(mystem_convert);
    Datum mystem_convert(PG_FUNCTION_ARGS) {
        if (PG_ARGISNULL(0)) {
            PG_RETURN_NULL();
        }
        
        std::string nrmLine;
        try {
            text *_line = PG_GETARG_TEXT_P(0);
            std::string line(VARDATA(_line), VARSIZE(_line) - VARHDRSZ);
            
            if (line.length() > 0) {
                pg_ms::inOutQueue_t inOutQueue;
                if (!inOutQueue.isOK()) {
                    PG_RETURN_NULL();
                }
                
                uint64_t id = 0;
                while (true) {
                    id = inOutQueue.setInQueueRecord(line);
                    if (id != 0) {
                        break;
                    } else {
                        usleep(1L);
                    }
                }
                
                while (true) {
                    if (inOutQueue.getOutQueueRecord(id, nrmLine)) {
                        break;
                    } else {
                        usleep(1L);
                    }
                }
            }
        } catch (const std::exception &_e) {
            elog(LOG, "MYSTEM: mystem_convert critical error: %s", _e.what());
        } catch (...) {
            elog(LOG, "MYSTEM: mystem_convert unknown critical error");
        }

        PG_RETURN_TEXT_P(cstring_to_text(nrmLine.c_str()));
    }
}
