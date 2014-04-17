/**
 * @file process/controllers/CollectionHandler.cpp
 * @author Ian Yang
 * @date Created <2011-01-25 18:40:12>
 */
#include <process/common/XmlConfigParser.h>
#include <process/common/CollectionManager.h>

#include <bundles/index/IndexSearchService.h>
#include <bundles/index/IndexTaskService.h>
#include <bundles/mining/MiningSearchService.h>
#include <bundles/mining/MiningTaskService.h>
#include <node-manager/DistributeRequestHooker.h>

#include "CollectionHandler.h"
#include "DocumentsGetHandler.h"
#include "DocumentsSearchHandler.h"

#include <common/Keys.h>
#include <common/JobScheduler.h>

#include <boost/bind.hpp>

namespace sf1r
{

using driver::Keys;

using namespace izenelib::driver;
using namespace izenelib::osgi;

CollectionHandler::CollectionHandler(const string& collection)
        : collection_(collection)
        , indexSearchService_(0)
        , indexTaskService_(0)
        , miningSearchService_(0)
        , miningTaskService_(0)
{
}

CollectionHandler::~CollectionHandler()
{
}

void CollectionHandler::search(::izenelib::driver::Request& request, ::izenelib::driver::Response& response)
{
    DocumentsSearchHandler handler(request,response,*this);
    handler.search();
}

void CollectionHandler::get(::izenelib::driver::Request& request, ::izenelib::driver::Response& response)
{
    DocumentsGetHandler handler(request, response,*this);
    handler.get();
}

bool CollectionHandler::create(const ::izenelib::driver::Value& document)
{
    //if(!indexTaskService_->HookDistributeRequest(false))
    //    return false;
    if (DistributeRequestHooker::get()->getHookType() != Request::FromAPI)
    {
        return indexTaskService_->createDocument(document);
    }
    task_type task = boost::bind(&IndexTaskService::createDocument, indexTaskService_, document);
    JobScheduler::get()->addTask(task, collection_);
    return true;
}

bool CollectionHandler::update(const ::izenelib::driver::Value& document)
{
    if (DistributeRequestHooker::get()->getHookType() != Request::FromAPI)
    {
        return indexTaskService_->updateDocument(document);
    }
    task_type task = boost::bind(&IndexTaskService::updateDocument, indexTaskService_, document);
    JobScheduler::get()->addTask(task, collection_);
    return true;
}

bool CollectionHandler::update_inplace(const ::izenelib::driver::Value& request)
{
    if (DistributeRequestHooker::get()->getHookType() != Request::FromAPI)
    {
        return indexTaskService_->updateDocumentInplace(request);
    }
    task_type task = boost::bind(&IndexTaskService::updateDocumentInplace, indexTaskService_, request);
    JobScheduler::get()->addTask(task, collection_);
    return true;
}

bool CollectionHandler::destroy(const ::izenelib::driver::Value& document)
{
    if (DistributeRequestHooker::get()->getHookType() != Request::FromAPI)
    {
        return indexTaskService_->destroyDocument(document);
    }
    task_type task = boost::bind(&IndexTaskService::destroyDocument, indexTaskService_, document);
    JobScheduler::get()->addTask(task, collection_);
    return true;
}

} // namespace sf1r
