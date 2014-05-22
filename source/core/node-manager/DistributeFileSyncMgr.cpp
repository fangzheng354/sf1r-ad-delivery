#include "DistributeFileSyncMgr.h"
#include "NodeManagerBase.h"
#include "SuperNodeManager.h"
#include "RequestLog.h"
#include "RecoveryChecker.h"
#include "DistributeTest.hpp"
#include "DistributeFileSys.h"

#include <net/distribute/DataTransfer2.hpp>
#include <configuration-manager/CollectionPath.h>
#include <sf1r-net/RpcServerConnection.h>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <glog/logging.h>
#include <boost/filesystem.hpp>
#include <boost/crc.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace bfs = boost::filesystem;
namespace ba = boost::asio;
using namespace boost::posix_time;

namespace sf1r
{

static void doTransferFile(const ReadyReceiveData& reqdata)
{
    izenelib::net::distribute::DataTransfer2 transfer(reqdata.receiver_ip, reqdata.receiver_port);
    FinishReceiveRequest req;
    if (bfs::file_size(reqdata.filepath) != 0 && not transfer.syncSend(reqdata.filepath, reqdata.filepath, false))
    {
        LOG(INFO) << "transfer file failed: " << reqdata.filepath;
        req.param_.success = false;
    }
    else
    {
        LOG(INFO) << "transfer file success: " << reqdata.filepath;
        req.param_.success = true;
    }
    req.param_.filepath = reqdata.filepath;
    DistributeFileSyncMgr::get()->sendFinishNotifyToReceiver(reqdata.receiver_rpcip, reqdata.receiver_rpcport, req);
}

static void getFileList(const std::string& dir, std::vector<std::string>& file_list,
    const std::set<std::string>& ignore_list, bool recrusive)
{
    static const bfs::directory_iterator end_it = bfs::directory_iterator();
    for( bfs::directory_iterator file(dir); file != end_it; ++file )
    {
        bfs::path current(file->path());
        if (bfs::is_directory(current))
        {
            if (recrusive)
                getFileList(current.string(), file_list, ignore_list, recrusive);
        }
        else if (bfs::is_regular_file(current))
        {
            if (ignore_list.find(current.filename().string()) != ignore_list.end())
            {
                LOG(INFO) << "ignore checking file: " << current;
                continue;
            }
            if (current.filename().string().find("removed.rollback") != std::string::npos)
                continue;
            file_list.push_back(current.string());
        }
        else
        {
            LOG(INFO) << "checking ignore file : " << current;
        }
    }
}

static uint32_t getFileCRC(const std::string& file, char* tmp_buf = NULL,
    size_t tmp_bufsize = 0, unsigned int check_level = 0)
{
    if (!bfs::exists(file) || check_level == 0)
        return 0;
    boost::crc_32_type crc_computer;
    size_t bufsize = tmp_bufsize;
    char *buf = tmp_buf;
    if (tmp_buf == NULL || tmp_bufsize == 0)
    {
        bufsize = 1024*1024*10;
        buf = new char[bufsize];
    }
    uint64_t total_readed = 0;
    uint64_t total_size = bfs::file_size(file);
    uint64_t max_check_data = check_level * 1024 * 1024 * 1024;
    bool need_skip = false;
    bool skipped = false;
    if (total_size > max_check_data*2)
    {
        need_skip = true;
    }
    try
    {
        ifstream ifs(file.c_str(), ios::binary);
        size_t readed = 0;
        while(ifs.good())
        {
            ifs.read(buf, bufsize);
            readed = ifs.gcount();
            crc_computer.process_bytes(buf, readed);
            if ((readed == 0) && ifs.eof())
                break;
            if (need_skip && !skipped)
            {
                total_readed += readed;
                if( total_readed >= max_check_data )
                {
                    ifs.seekg(max_check_data, ifs.end);
                    skipped = true;
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "error while get crc for file:" << file << ", err:" << e.what();
        if (tmp_buf == NULL || tmp_bufsize == 0)
            delete[] buf;
        return 0;
    }
    if (tmp_buf == NULL || tmp_bufsize == 0)
        delete[] buf;
    return crc_computer.checksum();
}

static std::string getFileCheckSum(const std::string& file, char* tmp_buf = NULL,
    size_t tmp_bufsize = 0, unsigned int check_level = 0)
{
    if (!bfs::exists(file))
        return "0_0";
    uint64_t filesize = bfs::file_size(file);
    uint32_t filecrc = getFileCRC(file, tmp_buf, tmp_bufsize, check_level);
    return boost::lexical_cast<std::string>(filesize) + "_" + boost::lexical_cast<std::string>(filecrc);
}

static void doReportStatus(const ReportStatusReqData& reqdata)
{
    ReportStatusRsp rsp_req;
    {
        boost::unique_lock<boost::mutex> guard(DistributeFileSyncMgr::get()->getFlushComputeLock());
        //
        // flush data first
        RecoveryChecker::get()->flushAllData();
        rsp_req.param_.rsp_host = SuperNodeManager::get()->getLocalHostIP();
        rsp_req.param_.success = true;
        rsp_req.param_.check_file_result.resize(reqdata.check_file_list.size());
        size_t bufsize = 1024*1024*32;
        LOG(INFO) << "got check request with check_level : " << reqdata.file_check_level;
        char* cal_buf = new char[bufsize];
        for (size_t i = 0; i < reqdata.check_file_list.size(); ++i)
        {
            const std::string& file = reqdata.check_file_list[i];
            if (DistributeFileSyncMgr::get()->getCachedCheckSum(file, rsp_req.param_.check_file_result[i]))
            {
                continue;
            }
            rsp_req.param_.check_file_result[i] = getFileCheckSum(file, cal_buf, bufsize, reqdata.file_check_level);
            DistributeFileSyncMgr::get()->updateCachedCheckSum(file, rsp_req.param_.check_file_result[i]);
            //LOG(INFO) << "file : " << file << ", checksum:" << rsp_req.param_.check_file_result[i];
        }
        delete[] cal_buf;

        DistributeTestSuit::getMemoryState(reqdata.check_key_list, rsp_req.param_.check_key_result);
        boost::shared_ptr<ReqLogMgr> reqlogmgr = RecoveryChecker::get()->getReqLogMgr();
        // check at most 10 million.
        uint32_t max_logid_checknum = reqlogmgr->getLastSuccessReqId();
        if (max_logid_checknum > 10000000)
            max_logid_checknum = 10000000;
        // get local redo log id 
        std::vector<std::string> logdata_list;
        LOG(INFO) << "report log id list from : " << reqdata.check_log_start_id << ", max check : " << max_logid_checknum;
        reqlogmgr->getReqLogIdList(reqdata.check_log_start_id, max_logid_checknum, false,
            rsp_req.param_.check_logid_list, logdata_list);
        if (rsp_req.param_.check_logid_list.empty())
            LOG(INFO) << "no any log on the node";
        else
            LOG(INFO) << "start log: " << rsp_req.param_.check_logid_list[0] << ", end log:" << rsp_req.param_.check_logid_list.back();

        // get local running collections.
        RecoveryChecker::get()->getCollList(rsp_req.param_.check_collection_list);
    }

    DistributeFileSyncMgr::get()->sendReportStatusRsp(reqdata.req_host, SuperNodeManager::get()->getFileSyncRpcPort(), rsp_req);
}

static void doGenMigrateSCD(const GenerateSCDReqData& reqdata)
{
    GenerateSCDRsp rsp_req;
    {
        rsp_req.param_.rsp_host = SuperNodeManager::get()->getLocalHostIP();
        rsp_req.param_.success = false;
        if (DistributeFileSyncMgr::get()->GenMigrateSCD(reqdata.coll,
                reqdata.migrate_vnode_list, rsp_req.param_.generated_insert_scds,
                rsp_req.param_.generated_del_scds))
        {
            rsp_req.param_.success = true;
        }
        else
        {
            LOG(ERROR) << "generate migrate scd file failed on " << rsp_req.param_.rsp_host;
        }
    }

    DistributeFileSyncMgr::get()->sendGenerateSCDRsp(reqdata.req_host,
        SuperNodeManager::get()->getFileSyncRpcPort(), rsp_req);
}

FileSyncServer::FileSyncServer(const std::string& host, uint16_t port, uint32_t threadNum)
    : host_(host)
    , port_(port)
    , threadNum_(threadNum)
    , threadpool_(threadNum_)
{
}

FileSyncServer::~FileSyncServer()
{
    std::cout << "~FileSyncServer()" << std::endl;
    stop();
}

void FileSyncServer::start()
{
    instance.listen(host_, port_);
    instance.start(threadNum_);
    LOG(INFO) << "starting file sync server on : " << host_ << ":" << port_;
}

void FileSyncServer::join()
{
    instance.join();
}

void FileSyncServer::run()
{
    start();
    join();
}

void FileSyncServer::stop()
{
    instance.end();
    instance.join();
    threadpool_.clear();
    threadpool_.wait();
}

void FileSyncServer::dispatch(msgpack::rpc::request req)
{
    try
    {
        std::string method;
        req.method().convert(&method);

        if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_TEST])
        {
            msgpack::type::tuple<bool> params;
            req.params().convert(&params);
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_REQLOG])
        {
            msgpack::type::tuple<GetReqLogData> params;
            req.params().convert(&params);
            GetReqLogData& reqdata = params.get<0>();
            // get 1000 new logs at most each time
            //
            reqdata.success = false;
            boost::shared_ptr<ReqLogMgr> reqlogmgr = RecoveryChecker::get()->getReqLogMgr();
            std::vector<uint32_t> logid_list;
            reqlogmgr->getReqLogIdList(reqdata.start_inc, 10000, true, logid_list, reqdata.logdata_list);
            if (logid_list.empty())
            {
                LOG(INFO) << "no more log after inc_id : " << reqdata.start_inc;
                reqdata.end_inc = reqdata.start_inc;
            }
            else
            {
                LOG(INFO) << "get log from inc_id : " << reqdata.start_inc;
                reqdata.end_inc = logid_list.back();;
                LOG(INFO) << "get log last inc_id :" << reqdata.end_inc; 
            }
            
            reqdata.success = true;
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_RUNNING_REQLOG])
        {
            msgpack::type::tuple<GetRunningReqLogData> params;
            req.params().convert(&params);
            GetRunningReqLogData& reqdata = params.get<0>();
            reqdata.success = false;
            reqdata.running_logdata = NodeManagerBase::get()->getSavedPackedData();
            reqdata.success = true;
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_SCD_LIST])
        {
            msgpack::type::tuple<GetSCDListData> params;
            req.params().convert(&params);
            GetSCDListData& reqdata = params.get<0>();
            // get all the scd file in the backup directory.
            reqdata.success = false;
            CollectionPath colpath;
            bool ret = RecoveryChecker::get()->getCollPath(reqdata.collection, colpath);
            if (ret)
            {
                reqdata.success = true;
                bfs::path backup_path = colpath.getScdPath();
                backup_path /= bfs::path("index/backup/");
                if (bfs::exists(backup_path))
                {
                    LOG(INFO) << "scanning backup index scd ....";
                    getFileList(backup_path.string(), reqdata.scd_list, std::set<std::string>(), false);
                }
                LOG(INFO) << "scanning recommend_scd....";
                bfs::path recommend_scd = colpath.getScdPath()/bfs::path("recommend/");
                if (bfs::exists(recommend_scd))
                {
                    // get scd file for recommend.
                    getFileList(recommend_scd.string(), reqdata.scd_list, std::set<std::string>(), true);
                }
            }
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_COLLECTION_FILE_LIST])
        {
            msgpack::type::tuple<GetCollectionFileListData> params;
            req.params().convert(&params);
            GetCollectionFileListData& reqdata = params.get<0>();
            reqdata.success = false;
            for (size_t i = 0; i < reqdata.collections.size(); ++i)
            {
                std::string colname = reqdata.collections[i];
                CollectionPath colpath;
                bool ret = RecoveryChecker::get()->getCollPath(colname, colpath);
                if (ret)
                {
                    reqdata.success = true;
                    bfs::path coldata_path = colpath.getCollectionDataPath();
                    // get all files.
                    getFileList(coldata_path.string(), reqdata.file_list, std::set<std::string>(), true);
                }
            }
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GET_FILE])
        {
            msgpack::type::tuple<GetFileData> params;
            req.params().convert(&params);
            GetFileData& reqdata = params.get<0>();
            reqdata.success = false;
            // get the file info 
            if (bfs::exists(reqdata.filepath) && bfs::is_regular_file(reqdata.filepath))
            {
                reqdata.success = true;
                reqdata.filesize = bfs::file_size(reqdata.filepath);
            }
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_READY_RECEIVE])
        {
            msgpack::type::tuple<ReadyReceiveData> params;
            req.params().convert(&params);
            ReadyReceiveData& reqdata = params.get<0>();
            reqdata.success = true;
            // start transfer thread.
            threadpool_.schedule(boost::bind(&doTransferFile, reqdata));
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_FINISH_RECEIVE])
        {
            msgpack::type::tuple<FinishReceiveData> params;
            req.params().convert(&params);
            FinishReceiveData& reqdata = params.get<0>();
            reqdata.success = true;
            DistributeFileSyncMgr::get()->notifyFinishReceive(reqdata.filepath);
            req.result(reqdata);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_REPORT_STATUS_REQ])
        {
            msgpack::type::tuple<ReportStatusReqData> params;
            req.params().convert(&params);
            ReportStatusReqData& reqdata = params.get<0>();
            threadpool_.schedule(boost::bind(&doReportStatus, reqdata));
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_REPORT_STATUS_RSP])
        {
            msgpack::type::tuple<ReportStatusRspData> params;
            req.params().convert(&params);
            ReportStatusRspData& rspdata = params.get<0>();
            DistributeFileSyncMgr::get()->notifyReportStatusRsp(rspdata);
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GENERATE_MIGRATE_SCD_REQ])
        {
            msgpack::type::tuple<GenerateSCDReqData> params;
            req.params().convert(&params);
            GenerateSCDReqData& reqdata = params.get<0>();
            threadpool_.schedule(boost::bind(&doGenMigrateSCD, reqdata));
            req.result(true);
        }
        else if (method == FileSyncServerRequest::method_names[FileSyncServerRequest::METHOD_GENERATE_MIGRATE_SCD_RSP])
        {
            msgpack::type::tuple<GenerateSCDRspData> params;
            req.params().convert(&params);
            GenerateSCDRspData& rspdata = params.get<0>();
            DistributeFileSyncMgr::get()->notifyGenerateSCDRsp(rspdata);
            req.result(true);
        }
        else
        {
            req.error(msgpack::rpc::NO_METHOD_ERROR);
        }
    }
    catch (const msgpack::type_error& e)
    {
        req.error(msgpack::rpc::ARGUMENT_ERROR);
    }
    catch (const std::exception& e)
    {
        req.error(std::string(e.what()));
    }
}

DistributeFileSyncMgr::DistributeFileSyncMgr()
{
    conn_mgr_ = new RpcServerConnection();
    RpcServerConnectionConfig config;
    config.rpcThreadNum = 8;
    conn_mgr_->init(config);
    ignore_list_.insert("LOG");
    ignore_list_.insert("LOG.old");
    ignore_list_.insert("cookie");
    ignore_list_.insert("CURRENT");
    ignore_list_.insert("barrels");

    // ignore for laser
    ignore_list_.insert("index.aidx");
    ignore_list_.insert("index.data");
    reporting_ = false;
}

void DistributeFileSyncMgr::init()
{
    if (!NodeManagerBase::get()->isDistributed())
        return;
    transfer_rpcserver_.reset(new FileSyncServer(SuperNodeManager::get()->getLocalHostIP(),
            SuperNodeManager::get()->getFileSyncRpcPort(), 4));
    transfer_rpcserver_->start();
    loadCachedCheckSum();
    NodeManagerBase::get()->setServerCheckCallback(boost::bind(&RpcServerConnection::testServer, conn_mgr_,
        _1, SuperNodeManager::get()->getFileSyncRpcPort()));
}

DistributeFileSyncMgr::~DistributeFileSyncMgr()
{
    stop();
}

void DistributeFileSyncMgr::stop()
{
    delete conn_mgr_;
    conn_mgr_ = NULL;
    if (transfer_rpcserver_)
        transfer_rpcserver_->stop();
    saveCachedCheckSum();
}

void DistributeFileSyncMgr::loadCachedCheckSum()
{
    std::ifstream ifs;
    ifs.open("./distributed_checksum");
    while (ifs.good())
    {
        std::string filepath;
        FileCheckData checkdata;
        //std::getline(ifs, line);
        ifs >> filepath >> checkdata.file_size >> checkdata.last_modify >> checkdata.check_sum;
        if (!filepath.empty() && !checkdata.check_sum.empty())
            cached_checksum_[filepath] = checkdata;
    }
    LOG(INFO) << "cached checksum loaded from file : " << cached_checksum_.size();
    ifs.close();
}

void DistributeFileSyncMgr::saveCachedCheckSum()
{
    std::ofstream ofs;
    ofs.open("./distributed_checksum");
    std::map<std::string, FileCheckData>::const_iterator cit = cached_checksum_.begin();
    while(cit != cached_checksum_.end())
    {
        ofs << cit->first << " " << cit->second.file_size << " " << cit->second.last_modify << " " << cit->second.check_sum << std::endl;
        ++cit;
    }
    ofs.close();
}

bool DistributeFileSyncMgr::getCachedCheckSum(const std::string& filepath, std::string& ret_checksum)
{
    try
    {
        boost::unique_lock<boost::mutex> lk(status_report_mutex_);
        std::map<std::string, FileCheckData>::const_iterator cit = cached_checksum_.find(filepath);
        if (cit != cached_checksum_.end())
        {
            if (!bfs::exists(filepath))
                return false;
            if (bfs::last_write_time(filepath) ==  cit->second.last_modify &&
                bfs::file_size(filepath) == cit->second.file_size)
            {
                ret_checksum = cit->second.check_sum;
                return true;
            }
        }
    }
    catch(const std::exception& e)
    {
        LOG(WARNING) << "get cached checksum error : " << e.what();
    }
    return false;
}

void DistributeFileSyncMgr::updateCachedCheckSum(const std::string& filepath, const std::string& checksum)
{
    if (filepath.empty() || checksum.empty() || !bfs::exists(filepath))
        return;
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    FileCheckData checkdata;
    checkdata.file_size = bfs::file_size(filepath);
    checkdata.last_modify = bfs::last_write_time(filepath);
    checkdata.check_sum = checksum;
    cached_checksum_[filepath] = checkdata;
}

void DistributeFileSyncMgr::notifyReportStatusRsp(const ReportStatusRspData& rspdata)
{
    //
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    status_rsp_list_.push_back(rspdata);
    status_report_cond_.notify_all();
}

void DistributeFileSyncMgr::sendReportStatusRsp(const std::string& ip, uint16_t port, const ReportStatusRsp& rsp)
{
    if (conn_mgr_ == NULL)
        return;
    bool rsp_ret = false;
    try
    {
        conn_mgr_->syncRequest(ip, port, rsp, rsp_ret);
    }
    catch(const std::exception& e)
    {
        LOG(ERROR) << "send report status response failed.";
    }
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    saveCachedCheckSum();
}

void DistributeFileSyncMgr::checkReplicasStatus(const std::vector<std::string>& colname_list,
    unsigned int check_level, std::string& check_errinfo)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return;

    {
        boost::unique_lock<boost::mutex> lk(status_report_mutex_);
        while (reporting_)
        {
            status_report_cond_.timed_wait(lk, boost::posix_time::seconds(5));
            check_errinfo = "there is already a report request processing.";
            LOG(INFO) << check_errinfo;
        }
        reporting_ = true;
    }
    check_errinfo = "";

    std::vector<std::string> replica_info;
    //NodeManagerBase::get()->getAllReplicaInfo(replica_info, true);
    std::string cur_primary_ip;
    if(!NodeManagerBase::get()->getCurrPrimaryInfo(cur_primary_ip))
    {
        boost::unique_lock<boost::mutex> lk(status_report_mutex_);
        reporting_ = false;
        if (!NodeManagerBase::get()->isOtherPrimaryAvailable())
        {
            return;
        }
        check_errinfo = "get other primary failed.";
        return;
    }
    replica_info.push_back(cur_primary_ip);

    uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();

    ReportStatusRequest req;
    req.param_.req_host = SuperNodeManager::get()->getLocalHostIP();
    req.param_.file_check_level = check_level;
    for (size_t i = 0; i < colname_list.size(); ++i)
    {
        CollectionPath colpath;
        bool ret = RecoveryChecker::get()->getCollPath(colname_list[i], colpath);
        if (!ret)
        {
            check_errinfo = "check collection not exist for  " + colname_list[i];
            LOG(INFO) << check_errinfo;
            boost::unique_lock<boost::mutex> lk(status_report_mutex_);
            reporting_ = false;
            return;
        }

        getFileList(colpath.getCollectionDataPath(), req.param_.check_file_list, ignore_list_, true);
    }
    boost::shared_ptr<ReqLogMgr> reqlogmgr = RecoveryChecker::get()->getReqLogMgr();
    getFileList(reqlogmgr->getRequestLogPath(), req.param_.check_file_list, ignore_list_, true);

    LOG(INFO) << "checking got file num :" << req.param_.check_file_list.size();
    DistributeTestSuit::getMemoryStateKeyList(req.param_.check_key_list);

    // check at most 10 million.
    uint32_t max_logid_checknum = reqlogmgr->getLastSuccessReqId();
    if (max_logid_checknum > 10000000)
        max_logid_checknum = 10000000;
    req.param_.check_log_start_id = reqlogmgr->getLastSuccessReqId() - max_logid_checknum;

    int wait_num = 0;
    for( size_t i = 0; i < replica_info.size(); ++i)
    {
        if (replica_info[i] == SuperNodeManager::get()->getLocalHostIP())
        {
            continue;
        }
        bool rsp_ret = false;
        try
        {
            conn_mgr_->syncRequest(replica_info[i], port, req, rsp_ret);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error while checking status: " << e.what();
            continue;
        }
        if (rsp_ret)
            ++wait_num;
    }

    std::vector<std::string> file_checksum_list(req.param_.check_file_list.size());
    std::vector<std::string> memory_state_list;
    DistributeTestSuit::getMemoryState(req.param_.check_key_list, memory_state_list);

    // calculate local.
    size_t bufsize = 1024*1024*32;
    char* cal_buf = new char[bufsize];
    for (size_t i = 0; i < req.param_.check_file_list.size(); ++i)
    {
        const std::string& file = req.param_.check_file_list[i];
        if (getCachedCheckSum(file, file_checksum_list[i]))
        {
            continue;
        }
        file_checksum_list[i] = getFileCheckSum(file, cal_buf, bufsize, req.param_.file_check_level);
        updateCachedCheckSum(file, file_checksum_list[i]);
        //LOG(INFO) << "file : " << file << ", checksum:" << file_checksum_list[i];
    }
    delete[] cal_buf;

    std::vector<std::string> check_collection_list;
    // get local redo log id 
    std::vector<uint32_t> check_logid_list;
    std::vector<std::string> check_logdata_list;
    reqlogmgr->getReqLogIdList(req.param_.check_log_start_id, max_logid_checknum, false, check_logid_list,
        check_logdata_list);

    if (check_logid_list.empty())
        LOG(INFO) << "no any log on the node";
    else
        LOG(INFO) << "start log: " << check_logid_list[0] << ", end log:" << check_logid_list.back();

    // get local running collections.
    RecoveryChecker::get()->getCollList(check_collection_list);

    bool is_file_mismatch = false;
    bool is_collection_mismatch = false;
    bool is_redolog_mismatch = false;
    int max_wait = 100;
    // wait for response.
    while(wait_num > 0)
    {
        std::vector<ReportStatusRspData> rspdata;
        {
            boost::unique_lock<boost::mutex> lk(status_report_mutex_);
            while (status_rsp_list_.empty())
            {
                if (--max_wait < 0)
                {
                    LOG(INFO) << "wait max time!! no longer wait, no rsp num: " << wait_num;
                    reporting_ = false;
                    check_errinfo = "wait report status timeout!!";
                    return;
                }
                status_report_cond_.timed_wait(lk, boost::posix_time::seconds(30));
            }
            // reset wait time if got any rsp.
            max_wait = 10;
            rspdata.swap(status_rsp_list_);
        }
        LOG(INFO) << "status report got rsp: " << rspdata.size();
        for(size_t i = 0; i < rspdata.size(); ++i)
        {
            LOG(INFO) << "checking rsp for host :" << rspdata[i].rsp_host;
            if (!rspdata[i].success)
            {
                LOG(WARNING) << "rsp return false from this host!!";
                continue;
            }
            if (rspdata[i].check_file_result.size() != file_checksum_list.size())
            {
                LOG(WARNING) << "rsp file check result size not matched!!!!";
                continue;
            }
            for (size_t j = 0; j < file_checksum_list.size(); ++j)
            {
                if (file_checksum_list[j] != rspdata[i].check_file_result[j])
                {
                    // exclude leveldb path, leveldb need check special.
                    std::string name = bfs::path(req.param_.check_file_list[j]).filename().string();
                    if (name.find("MANIFEST-") == 0 || name.find(".sst") == 6 || name.find(".log") == 6)
                    {
                        // the file of level db can be ignored.
                        continue;
                    }
                    // exclude tc hash db file.
                    // exclude tpc file.
                    if ( name.length() > 4 &&
                         (name.substr(name.length() - 4) == ".tch" ||
                          name.substr(name.length() - 4) == ".tpc" ||
                          name.substr(name.length() - 4) == ".ldb" ) )
                    {
                        //LOG(WARNING) << "tc hash db, tpc file, leveldb file ignored: " << name;
                        continue;
                    }
                    LOG(WARNING) << "one of file not the same as local : " << req.param_.check_file_list[j];
                    LOG(INFO) << "local : " << file_checksum_list[j] << " VS " << rspdata[i].check_file_result[j];
                    is_file_mismatch =  true;
                }
            }
            if (rspdata[i].check_key_result.size() != memory_state_list.size())
            {
                LOG(WARNING) << "rsp memory state result size not matched!!!!";
                continue;
            }
            for (size_t j = 0; j < memory_state_list.size(); ++j)
            {
                if (memory_state_list[j] != rspdata[i].check_key_result[j])
                {
                    LOG(WARNING) << "one of memory state not the same as local : " << req.param_.check_key_list[j];
                }
            }
            if (rspdata[i].check_logid_list.size() != check_logid_list.size())
            {
                LOG(ERROR) << "rsp logid list size not matched local, " << rspdata[i].check_logid_list.size() << "," << check_logid_list.size();
                is_redolog_mismatch = true;
                continue;
            }
            for (size_t j = 0; j < check_logid_list.size(); ++j)
            {
                if (rspdata[i].check_logid_list[j] != check_logid_list[j])
                {
                    LOG(ERROR) << "rsp logid list id not match, " << rspdata[i].check_logid_list[j] << "," << check_logid_list[j];
                    is_redolog_mismatch = true;
                }
            }
            if (rspdata[i].check_collection_list.size() != check_collection_list.size())
            {
                LOG(ERROR) << "rsp running collection list size not matched local, " << rspdata[i].check_collection_list.size() << "," << check_collection_list.size();
                is_collection_mismatch = true;
                continue;
            }
            for (size_t j = 0; j < check_collection_list.size(); ++j)
            {
                if (rspdata[i].check_collection_list[j] != check_collection_list[j])
                {
                    LOG(ERROR) << "rsp running collection list id not match, " << rspdata[i].check_collection_list[j] << "," << check_collection_list[j];
                    is_collection_mismatch = true;
                }
            }

        }
        wait_num -= rspdata.size();
    }
    if (is_file_mismatch)
    {
        check_errinfo += " at least one of file not the same status between replicas. ";
    }
    if (is_collection_mismatch)
    {
        check_errinfo += " at least one of running collection not the same between replicas.";
    }
    if (is_redolog_mismatch)
    {
        check_errinfo += " at least one of logid not the same between replicas.";
    }
    LOG(INFO) << "report request finished";
    boost::unique_lock<boost::mutex> lk(status_report_mutex_);
    reporting_ = false;
    saveCachedCheckSum();
}

bool DistributeFileSyncMgr::getCurrentRunningReqLog(std::string& saved_log)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return true;
    int retry = 3;
    saved_log.clear();
    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrPrimaryInfo(ip))
        {
            LOG(INFO) << "get primary sync server failed.";
            return false;
        }
        if (ip == SuperNodeManager::get()->getLocalHostIP())
        {
            LOG(INFO) << "the ip is the same as local : " << ip;
            return false;
        }

        LOG(INFO) << "try get running log from: " << ip << ":" << port;
        GetRunningReqLogRequest req;
        GetRunningReqLogData rsp;
        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }
        if (rsp.success)
        {
            saved_log = rsp.running_logdata;
            return true;
        }
    }
    LOG(INFO) << "get current running log data failed .";
    return false;
}

bool DistributeFileSyncMgr::getNewestReqLog(bool from_primary_only, uint32_t start_from, std::vector<std::string>& saved_log)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return true;
    // note : for optimize, we can get log from any node that has entered the cluster and 
    // current state is ready.
    int retry = 3;

    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        // because all replica node should have the same config,
        // so the rpc ports for all file sync servers are the same.
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();

        if (from_primary_only)
        {
            if(!NodeManagerBase::get()->getCurrPrimaryInfo(ip))
            {
                LOG(INFO) << "get primary sync server failed.";
                std::vector<std::string>().swap(saved_log);
                return false;
            }
        }
        else
        {
            if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
            {
                LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
                std::vector<std::string>().swap(saved_log);
                return true;
            }
        }
        if (ip == SuperNodeManager::get()->getLocalHostIP())
        {
            LOG(INFO) << "the ip is the same as local : " << ip;
            std::vector<std::string>().swap(saved_log);
            return false;
        }
        LOG(INFO) << "try get newest log from: " << ip << ":" << port;
        GetReqLogRequest req;
        req.param_.start_inc = start_from;
        GetReqLogData rsp;

        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }
        if (rsp.success)
        {
            saved_log.swap(rsp.logdata_list);
            return true;
        }
    }
    LOG(INFO) << "get newest log data failed .";
    return false;
}

bool DistributeFileSyncMgr::syncCollectionData(const std::vector<std::string>& colname_list)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return true;

    int retry = 3;
    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return true;
        }
        LOG(INFO) << "try get collection file list from: " << ip << ":" << port;

        GetCollectionFileListRequest req;
        req.param_.collections = colname_list;
        GetCollectionFileListData rsp;
        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }

        if (rsp.success)
        {
            if (rsp.file_list.empty())
            {
                LOG(INFO) << "no collection file need sync.";
                return true;
            }
            for (size_t i = 0; i < rsp.file_list.size(); ++i)
            {
                GetFileData file_rsp;
                file_rsp.filepath = rsp.file_list[i];
                if (!getFileInfo(ip, port, file_rsp))
                    break;

                if(!getFileFromOther(ip, port, file_rsp.filepath, file_rsp.filesize))
                {
                    LOG(INFO) << "get file from other failed, retry next." << file_rsp.filepath;
                    break;
                }
                LOG(INFO) << "a collection file finished :" << file_rsp.filepath;
                if (i == rsp.file_list.size() - 1)
                    return true;
            }
        }
    }
    LOG(INFO) << "sync to newest collection file failed.";
    return false;
}

bool DistributeFileSyncMgr::syncNewestSCDFileList(const std::string& colname)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return true;
    if (DistributeFileSys::get()->isEnabled())
    {
        LOG(INFO) << "DFS enabled, no need sync scd files.";
        return true;
    }
    // get the backup scd file list used for index.
    
    // get primary file sync ip:port
    //
    // send rpc request to get scd filelist
    //
    // receiver response that will contain the filelist.
    // for each file in list, send rpc request to get file info,
    // if file is the same as local, we think it already exist and
    // continue to get next.
    int retry = 3;
    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return true;
        }
        LOG(INFO) << "try get scd file list from: " << ip << ":" << port;
        GetSCDListRequest req;
        req.param_.collection = colname;
        GetSCDListData rsp;
        try
        {
            conn_mgr_->syncRequest(ip, port, req, rsp);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error, will retry : " << e.what();
            continue;
        }

        if (rsp.success)
        {
            if (rsp.scd_list.empty())
            {
                LOG(INFO) << "no scd file need sync.";
                return true;
            }
            for (size_t i = 0; i < rsp.scd_list.size(); ++i)
            {
                GetFileData file_rsp;
                file_rsp.filepath = rsp.scd_list[i];
                if (!getFileInfo(ip, port, file_rsp))
                    break;

                if(!getFileFromOther(ip, port, file_rsp.filepath, file_rsp.filesize))
                {
                    LOG(INFO) << "get file from other failed, retry next." << file_rsp.filepath;
                    break;
                }
                //LOG(INFO) << "a scd file finished :" << file_rsp.filepath;
                if (i == rsp.scd_list.size() - 1)
                    return true;
            }
        }
    }
    LOG(INFO) << "sync to newest scd file list failed.";
    return false;
}

bool DistributeFileSyncMgr::pushFileToAllReplicas(const std::string& srcpath,
    const std::string& destpath, bool recrusive)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return true;
    std::vector<std::string> replica_info;
    NodeManagerBase::get()->getAllReplicaInfo(replica_info);
    bool all_success = true;
    uint16_t port = SuperNodeManager::get()->getDataReceiverPort();
    for (size_t i = 0; i < replica_info.size(); ++i)
    {
        izenelib::net::distribute::DataTransfer2 transfer(replica_info[i], port);
        if (not transfer.syncSend(srcpath, destpath, recrusive))
        {
            LOG(WARNING) << "push file to replica failed: " << replica_info[i];
            all_success = false;
        }
        LOG(INFO) << "push file to replica success : " << replica_info[i];
    }
    return all_success;
}

bool DistributeFileSyncMgr::getFileInfo(const std::string& ip, uint16_t port, GetFileData& file_rsp)
{
    if (conn_mgr_ == NULL)
        return false;
    LOG(INFO) << "try get file from: " << ip << ":" << port;
    GetFileRequest file_req;
    file_req.param_.filepath = file_rsp.filepath;
    try
    {
        conn_mgr_->syncRequest(ip, port, file_req, file_rsp);
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "send request error while get file info : " << e.what();
        return false;
    }
    if (!file_rsp.success)
    {
        LOG(INFO) << "get file info failed for :" << file_rsp.filepath;
        return false;
    }
    return true;
}

bool DistributeFileSyncMgr::getFileFromOther(const std::string& filepath, bool force_overwrite)
{
    int retry = 3;
    srand( time(NULL) );

    while(retry-- > 0)
    {
        std::string ip;
        uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();
        if(!NodeManagerBase::get()->getCurrNodeSyncServerInfo(ip, rand()))
        {
            LOG(INFO) << "get file sync server failed. This may happen if only one sf1r node.";
            return false;
        }
        GetFileData file_rsp;
        file_rsp.filepath = filepath;
        if (!getFileInfo(ip, port, file_rsp))
            continue;

        if(!getFileFromOther(ip, port, file_rsp.filepath, file_rsp.filesize, force_overwrite))
        {
            LOG(INFO) << "get file from other failed, retry next." << file_rsp.filepath;
            continue;
        }

        //LOG(INFO) << "get file finished :" << file_rsp.filepath;
        return true;
    }
    return false;
}

bool DistributeFileSyncMgr::getFileFromOther(const std::string& ip, uint16_t port,
    const std::string& filepath, uint64_t filesize, bool force_overwrite)
{
    if (!transfer_rpcserver_ || conn_mgr_ == NULL)
        return false;

    if (bfs::exists(filepath))
    {
        if (!bfs::is_regular_file(filepath))
        {
            LOG(INFO) << "found a file with same name but not a regular file." << filepath;
            bfs::remove_all(filepath + "_renamed");
            bfs::rename(filepath, filepath + "_renamed");
        }
        else
        {
            if(bfs::file_size(filepath) == filesize)
            {
                //LOG(INFO) << "local file is the same size : " << filepath;
                if (!force_overwrite)
                    return true;
            }
            bfs::remove(filepath);
        }
    }
    else if (filesize == 0)
    {
        LOG(INFO) << "remote file is a empty file : " << filepath;
        std::ofstream ofs;
        ofs.open(filepath.c_str());
        ofs << std::flush;
        ofs.close();
        return true;
    }

    ReadyReceiveRequest req;
    req.param_.receiver_ip = SuperNodeManager::get()->getLocalHostIP();
    req.param_.receiver_port = SuperNodeManager::get()->getDataReceiverPort();
    req.param_.receiver_rpcip = transfer_rpcserver_->getHost();
    req.param_.receiver_rpcport = transfer_rpcserver_->getPort();
    req.param_.filepath = filepath;
    req.param_.filesize = filesize;
    
    {
        // prepare wait data.
        boost::unique_lock<boost::mutex> lk(mutex_);
        if (wait_finish_notify_.find(filepath) != wait_finish_notify_.end())
        {
            LOG(INFO) << "file receiver is already waiting : " << filepath;
            return false;
        }
        wait_finish_notify_[filepath] = false;
    }

    ReadyReceiveData rsp;
    try
    {
        conn_mgr_->syncRequest(ip, port, req, rsp);
    }
    catch(const std::exception& e)
    {
        LOG(INFO) << "send request error while get file from other: " << e.what();
        return false;
    }

    // wait receiver.
    boost::unique_lock<boost::mutex> lk(mutex_);
    while(!wait_finish_notify_[filepath])
    {
        cond_.wait(lk);
    }
    LOG(INFO) << "a file finished receive : " << filepath;
    wait_finish_notify_.erase(filepath);
    if (bfs::exists(filepath) && bfs::is_regular_file(filepath) && bfs::file_size(filepath) == filesize)
        return true;
    LOG(WARNING) << "file failed pass check: " << filepath;
    return false;
}

void DistributeFileSyncMgr::sendFinishNotifyToReceiver(const std::string& ip, uint16_t port, const FinishReceiveRequest& req)
{
    if (conn_mgr_ == NULL)
        return;
    FinishReceiveData rsp;
    try
    {
        conn_mgr_->syncRequest(ip, port, req, rsp);
    }
    catch(const std::exception& e)
    {
        LOG(ERROR) << "send request error while notify finish to other: " << e.what();
    }
}

void DistributeFileSyncMgr::notifyFinishReceive(const std::string& filepath)
{
    boost::unique_lock<boost::mutex> lk(mutex_);
    if (wait_finish_notify_.find(filepath) == wait_finish_notify_.end())
    {
        LOG(INFO) << "a file finish notify but not waiting: " << filepath;
        return;
    }
    wait_finish_notify_[filepath] = true;
    cond_.notify_all();
    LOG(INFO) << "a file finish notify for : " << filepath;
}

bool DistributeFileSyncMgr::generateMigrateScds(const std::string& coll,
    const std::map<std::string, std::map<shardid_t, std::vector<vnodeid_t> > >& from,
    std::map<shardid_t, std::vector<std::string> >& generated_insert_scds,
    std::map<shardid_t, std::vector<std::string> >& generated_del_scds)
{
    if (!NodeManagerBase::get()->isDistributed() || conn_mgr_ == NULL)
        return false;
    if (!DistributeFileSys::get()->isEnabled())
        return false;

    uint16_t port = SuperNodeManager::get()->getFileSyncRpcPort();

    {
        boost::unique_lock<boost::mutex> lk(generate_scd_mutex_);
        generate_scd_rsp_list_.clear();
    }

    std::map<shardid_t, std::vector<vnodeid_t> > local_scds;
    int wait_num = 0;
    for(std::map<std::string, std::map<shardid_t, std::vector<vnodeid_t> > >::const_iterator cit = from.begin();
        cit != from.end(); ++cit)
    {
        GenerateSCDRequest req;
        req.param_.req_host = SuperNodeManager::get()->getLocalHostIP();
        req.param_.coll = coll;
        req.param_.migrate_vnode_list = cit->second;
        if (cit->first == SuperNodeManager::get()->getLocalHostIP())
        {
            local_scds = cit->second;
            continue;
        }
        bool rsp_ret = false;
        try
        {
            conn_mgr_->syncRequest(cit->first, port, req, rsp_ret);
        }
        catch(const std::exception& e)
        {
            LOG(INFO) << "send request error while checking status: " << e.what()
                << ", ip: " << cit->first;
            return false;
        }
        if (rsp_ret)
            ++wait_num;
    }

    generated_insert_scds.clear();
    generated_del_scds.clear();
    if (!local_scds.empty())
    {
        std::map<shardid_t, std::string> local_insert_scds;
        std::map<shardid_t, std::string> local_del_scds;

        // generate the migrate scds on the current node.
        if(!scd_generator_(coll, local_scds, local_insert_scds, local_del_scds))
        {
            LOG(INFO) << "generate the migrate scd files on local failed.";
            return false;
        }

        for (std::map<shardid_t, std::string>::const_iterator scdit = local_insert_scds.begin();
            scdit != local_insert_scds.end(); ++scdit)
        {
            generated_insert_scds[scdit->first].push_back(scdit->second);
        }
        for (std::map<shardid_t, std::string>::const_iterator scdit = local_del_scds.begin();
            scdit != local_del_scds.end(); ++scdit)
        {
            generated_del_scds[scdit->first].push_back(scdit->second);
        }
    }
    int max_wait = 500;
    // wait for response.
    while(wait_num > 0)
    {
        std::vector<GenerateSCDRspData> rspdata;
        {
            boost::unique_lock<boost::mutex> lk(generate_scd_mutex_);
            while (generate_scd_rsp_list_.empty())
            {
                if (--max_wait < 0)
                {
                    LOG(INFO) << "wait max time!! no longer wait, no rsp num: " << wait_num;
                    return false;
                }
                LOG(INFO) << "waiting generated scd files ...";
                generate_scd_cond_.timed_wait(lk, boost::posix_time::seconds(30));
            }
            // reset wait time if got any rsp.
            max_wait = 30;
            rspdata.swap(generate_scd_rsp_list_);
        }
        LOG(INFO) << "status report got rsp: " << rspdata.size();
        for(size_t i = 0; i < rspdata.size(); ++i)
        {
            LOG(INFO) << "checking rsp for host :" << rspdata[i].rsp_host;
            if (!rspdata[i].success)
            {
                LOG(WARNING) << "rsp return false from this host!!";
                return false;
            }
            for (std::map<shardid_t, std::string>::const_iterator scdit = rspdata[i].generated_insert_scds.begin();
                scdit != rspdata[i].generated_insert_scds.end(); ++scdit)
            {
                generated_insert_scds[scdit->first].push_back(scdit->second);
            }
            for (std::map<shardid_t, std::string>::const_iterator scdit = rspdata[i].generated_del_scds.begin();
                scdit != rspdata[i].generated_del_scds.end(); ++scdit)
            {
                generated_del_scds[scdit->first].push_back(scdit->second);
            }
        }
        wait_num -= rspdata.size();
    }
    LOG(INFO) << "generate migrate scd request finished";
    return true;
}

void DistributeFileSyncMgr::notifyGenerateSCDRsp(const GenerateSCDRspData& rspdata)
{
    boost::unique_lock<boost::mutex> lk(generate_scd_mutex_);
    generate_scd_rsp_list_.push_back(rspdata);
    generate_scd_cond_.notify_all();
}

void DistributeFileSyncMgr::sendGenerateSCDRsp(const std::string& ip, uint16_t port, const GenerateSCDRsp& rsp)
{
    if (conn_mgr_ == NULL)
        return;
    bool rsp_ret = false;
    try
    {
        conn_mgr_->syncRequest(ip, port, rsp, rsp_ret);
    }
    catch(const std::exception& e)
    {
        LOG(ERROR) << "send response failed to host : " << ip;
    }
}

}
