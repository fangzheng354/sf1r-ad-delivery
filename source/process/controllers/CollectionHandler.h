#ifndef PROCESS_CONTROLLERS_COLLECTIONHANDLER_H
#define PROCESS_CONTROLLERS_COLLECTIONHANDLER_H
/**
 * @file process/controllers/CollectionHandler.h
 * @author Ian Yang
 * @date Created <2011-01-25 17:30:12>
 */

#include <bundles/index/IndexBundleConfiguration.h>
#include <bundles/mining/MiningBundleConfiguration.h>

#include <util/driver/Request.h>
#include <util/driver/Response.h>
#include <util/driver/Value.h>
#include <util/driver/Controller.h>

namespace sf1r
{
class IndexTaskService;
class IndexSearchService;
class MiningSearchService;
class MiningTaskService;

/**
 * @brief CollectionHandler
 *
 * Each collection has its corresponding collectionHandler
 *
 */
class CollectionHandler
{
public:
    CollectionHandler(const std::string& collection);

    ~CollectionHandler();
public:
    //////////////////////////////////////////
    //    Handlers
    //////////////////////////////////////////
    void search(::izenelib::driver::Request& request, ::izenelib::driver::Response& response);
    void get(::izenelib::driver::Request& request, ::izenelib::driver::Response& response);
    bool create(const ::izenelib::driver::Value& document);
    bool update(const ::izenelib::driver::Value& document);
    bool update_inplace(const ::izenelib::driver::Value& request);
    bool destroy(const ::izenelib::driver::Value& document);
    
    void laserRecommend(::izenelib::driver::Request& request, ::izenelib::driver::Response& response);

    //////////////////////////////////////////
    //    Helpers
    //////////////////////////////////////////
    void registerService(IndexSearchService* service)
    {
        indexSearchService_ = service;
    }

    void registerService(IndexTaskService* service)
    {
        indexTaskService_ = service;
    }

    void registerService(MiningSearchService* service)
    {
        miningSearchService_ = service;
    }

    void registerService(MiningTaskService* service)
    {
        miningTaskService_ = service;
    }

    void setBundleSchema(IndexBundleSchema& schema)
    {
        indexSchema_ = schema;
    }

    void setBundleSchema(MiningSchema& schema)
    {
        miningSchema_ = schema;
    }

    void setBundleSchema(ZambeziConfig& schema)
    {
        zambeziConfig_ = schema;
    }

    void setDocumentSchema(const DocumentSchema& schema)
    {
        documentSchema_ = schema;
    }

public:
    std::string collection_;

    IndexSearchService* indexSearchService_;

    IndexTaskService* indexTaskService_;

    MiningSearchService* miningSearchService_;

    MiningTaskService* miningTaskService_;

    DocumentSchema documentSchema_;

    IndexBundleSchema indexSchema_;

    ZambeziConfig zambeziConfig_;

    MiningSchema miningSchema_;
};

} // namespace sf1r

#endif // PROCESS_CONTROLLERS_COLLECTIONHANDLER_H
