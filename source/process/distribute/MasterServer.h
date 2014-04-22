#ifndef PROCESS_DISTRIBUTE_MASTER_SERVER_H_
#define PROCESS_DISTRIBUTE_MASTER_SERVER_H_

#include <3rdparty/msgpack/rpc/server.h>

#include <common/ResultType.h>
#include <query-manager/ActionItem.h>
#include <aggregator-manager/MasterServerConnector.h>
#include <aggregator-manager/MasterNotifier.h>
#include <common/CollectionManager.h>
#include <controllers/CollectionHandler.h>
#include <index/IndexSearchService.h>

namespace sf1r
{

/**
 * Search/Recommend Master RPC Server
 */
class MasterServer : public msgpack::rpc::server::base
{
public:
    ~MasterServer()
    {
        stop();
    }

    void start(const std::string& host, uint16_t port, unsigned int threadnum = 30)
    {
        instance.listen(host, port);
        instance.start(threadnum);
    }

    void stop()
    {
        if (instance.is_running())
        {
            instance.end();
            instance.join();
        }
    }

    void dispatch(msgpack::rpc::request req)
    {
        try
        {
            std::string method;
            req.method().convert(&method);

            if (method == MasterServerConnector::Methods_[MasterServerConnector::Method_getDocumentsByIds_])
            {
                getDocumentsByIds(req);
            }
            else if (method == MasterServerConnector::Methods_[MasterServerConnector::Method_documentSearch_])
            {
                documentSearch(req);
            }
            else if (method == "notify")
            {
                notify(req);
            }
            else
            {
                std::string errinfo = "unsupported method : " +  method;
                LOG(WARNING) << errinfo;
                req.error(errinfo);
            }
        }
        catch (msgpack::type_error& e)
        {
            req.error(msgpack::rpc::ARGUMENT_ERROR);
            LOG(ERROR) << "#[Master] notified, Argument error!"<< e.what() << endl;
        }
        catch (std::exception& e)
        {
            LOG(ERROR) << "#[Master] notified, "<< e.what() << endl;
            req.error(std::string(e.what()));
        }
    }

private:
    /**
     * @example rpc call
     *   GetDocumentsByIdsActionItem action;
     *   RawTextResultFromSIA resultItem;
     *   MasterServerConnector::get()->syncCall(MasterServerConnector::Method_getDocumentsByIds_, action, resultItem);
     */
    void getDocumentsByIds(msgpack::rpc::request& req)
    {
        msgpack::type::tuple<GetDocumentsByIdsActionItem> params;
        req.params().convert(&params);
        GetDocumentsByIdsActionItem action = params.get<0>();

        CollectionManager::MutexType* mutex = CollectionManager::get()->getCollectionMutex(action.collectionName_);
        CollectionManager::ScopedReadLock lock(*mutex);
        CollectionHandler* collectionHandler = CollectionManager::get()->findHandler(action.collectionName_);
        if (collectionHandler)
        {
            RawTextResultFromSIA resultItem;
            collectionHandler->indexSearchService_->getDocumentsByIds(action, resultItem);
            if (!resultItem.error_.empty())
            {
                LOG(ERROR) << "get documents failed.";
                action.print();
            }
            req.result(resultItem);
        }
        else
        {
            std::string error = "No collectionHandler found for " + action.collectionName_;
            req.error(error);
        }
    }

    void documentSearch(msgpack::rpc::request& req)
    {
        msgpack::type::tuple<KeywordSearchActionItem> params;
        req.params().convert(&params);
        KeywordSearchActionItem action = params.get<0>();

        CollectionManager::MutexType* mutex = CollectionManager::get()->getCollectionMutex(action.collectionName_);
        CollectionManager::ScopedReadLock lock(*mutex);
        CollectionHandler* collectionHandler = CollectionManager::get()->findHandler(action.collectionName_);
        if (collectionHandler)
        {
            KeywordSearchResult resultItem;
            bool ret = collectionHandler->indexSearchService_->getSearchResult(action, resultItem);
            if (!ret)
            {
                LOG(ERROR) << "search documents failed in master.";
                action.print();
            }
            req.result(resultItem);
        }
        else
        {
            std::string error = "No collectionHandler found for " + action.collectionName_;
            LOG(ERROR) << error;
            req.error(error);
        }

    }

    void notify(msgpack::rpc::request& req)
    {
        msgpack::type::tuple<NotifyMSG> params;
        req.params().convert(&params);
        NotifyMSG msg = params.get<0>();

        if (msg.method == "CLEAR_SEARCH_CACHE")
        {
            CollectionManager::MutexType* mutex = CollectionManager::get()->getCollectionMutex(msg.collection);
            CollectionManager::ScopedReadLock lock(*mutex);
            CollectionHandler* collectionHandler = CollectionManager::get()->findHandler(msg.collection);
            if (collectionHandler)
            {
                collectionHandler->indexSearchService_->OnUpdateSearchCache();
            }
            else
            {
                std::string error = "No collectionHandler found for " + msg.collection;
                req.error(error);
            }
        }
    }
};

}

#endif /* PROCESS_DISTRIBUTE_MASTER_SERVER_H_ */
