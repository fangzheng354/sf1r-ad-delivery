#include "LogAnalysisController.h"

#include <log-manager/SystemEvent.h>
#include <log-manager/UserQuery.h>
#include <common/parsers/OrderArrayParser.h>
#include <common/parsers/ConditionArrayParser.h>

#include <list>
#include <vector>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/any.hpp>

using namespace std;
using namespace boost;
using namespace boost::posix_time;

namespace sf1r {

using driver::Keys;
using namespace izenelib::driver;
std::string LogAnalysisController::parseSelect()
{
    vector<string> results;
    if (!nullValue(request()[Keys::select])) {
        const Value::ArrayType* array = request()[Keys::select].getPtr<Value::ArrayType>();
        if (array) {
            for (std::size_t i = 0; i < array->size(); ++i) {
                std::string column = asString((*array)[i]);
                boost::to_lower(column);
                results.push_back(column);
            }
        }
    }
    return str_join(results, ",");
}

std::string LogAnalysisController::parseOrder()
{
    vector<string> results;
    OrderArrayParser orderArrayParser;
    if (!nullValue(request()[Keys::sort])) {
        orderArrayParser.parse(request()[Keys::sort]);
        for( vector<OrderParser>::const_iterator it = orderArrayParser.parsedOrders().begin();
            it != orderArrayParser.parsedOrders().end(); it++ ) {
                results.push_back( str_concat(it->property(), " ", it->ascendant() ? "ASC" : "DESC") );
        }
    }
    return str_join(results, ",");
}

std::string LogAnalysisController::parseConditions()
{
    vector<string> results;
    ConditionArrayParser conditonArrayParser;
    if (!nullValue(request()[Keys::conditions])) {
        conditonArrayParser.parse(request()[Keys::conditions]);
        for( vector<ConditionParser>::const_iterator it = conditonArrayParser.parsedConditions().begin();
            it != conditonArrayParser.parsedConditions().end(); it++ ) {
                if( (it->op() == "=" || it->op() == "<"  || it->op() == ">" ||
                    it->op() == "<>" || it->op() == ">=" || it->op() == "<=") && it->size() == 1 ) {
                    results.push_back( str_concat(it->property(), it->op(), to_str((*it)(0))) );
                } else if (it->op() == "between" && it->size() == 2 ) {
                    stringstream ss;
                    ss << it->property() << " between " << to_str((*it)(0)) << " and " << to_str((*it)(1));
                    results.push_back( ss.str() );
                } else if (it->op() == "in" && it->size() > 0 ) {
                    stringstream ss;
                    ss << it->property() << " in(";
                    for(size_t i=0; i<it->size()-1; i++) {
                        ss << to_str((*it)(i)) << ",";
                    }
                    ss <<  to_str((*it)(it->size()-1)) << ")";
                    results.push_back(ss.str());
                }
        }
    }
    return str_join(results, " and ");
}

/**
 * @brief Action \b system_events.
 *
 * @section request
 *
 * - @b select (@c Array): Select columns in result. OrderArrayParser.
 * - @b conditions (@c Array): Result filtering conditions. See ConditionArrayParser.
 * - @b sort (@c Array): Sort result. See OrderArrayParser.
 *
 * @section response
 *
 * - @b system_events All system events which fit conditions.
 *
 * @section example
 *
 * Request
 * @code
 * {
 *   "select"=>["source", "content", "timestamp"],
 *   "conditions"=>[
        {"property":"timestamp", "operator":"range", "value":[1.0, 10.0]},
        {"property":"level", "operator":"in", "value":["warn", "error"]}
     ],
     "sort"=>["timestamp"]
 * }
 * @endcode
 *
 * response
 * @code
 * {
 *   "system_events" => [
 *      {"level"=>"error", "source"=>"SYS", "content"=>"Out of memory", "timestamp"=>"2010-Jul-16 18:11:38" }
 *   ]
 * }
 * @endcode
 *
 */
void LogAnalysisController::system_events()
{
    string select = parseSelect();
    string conditions = parseConditions();
    string order = parseOrder();
    vector<SystemEvent> results;

    if( !SystemEvent::find(select, conditions, order, results) ) {
        response().addError("[LogManager] Fail to process such a request");
        return;
    }

    Value& systemEvents = response()[Keys::system_events];
    systemEvents.reset<Value::ArrayType>();
    for (vector<SystemEvent>::iterator it = results.begin(); it != results.end(); it ++ )
    {
        Value& systemEvent = systemEvents();
        if(it->hasLevel()) systemEvent[Keys::level] = it->getLevel();
        if(it->hasSource()) systemEvent[Keys::source] = it->getSource();
        if(it->hasContent()) systemEvent[Keys::content] = it->getContent();
        if(it->hasTimeStamp()) systemEvent[Keys::timestamp] = to_simple_string(it->getTimeStamp());
    }
}

/**
 * @brief Action \b user_queries.
 *
 * @section request
 *
 * - @b select (@c Array): Select columns in result. OrderArrayParser.
 * - @b conditions (@c Array): Result filtering conditions. See ConditionArrayParser.
 * - @b sort (@c Array): Sort result. See OrderArrayParser.
 *
 * @section response
 *
 * - @b user_queries All system events which fit conditions.
 *
 */

void LogAnalysisController::user_queries()
{
    string select = parseSelect();
    string conditions = parseConditions();
    string order = parseOrder();
    vector<UserQuery> results;

    if( !UserQuery::find(select, conditions, order, results) ) {
        response().addError("[LogManager] Fail to process such a request");
        return;
    }

    Value& userQueries = response()[Keys::user_queries];
    userQueries.reset<Value::ArrayType>();
    for ( vector<UserQuery>::iterator it = results.begin(); it != results.end(); it ++ )
    {
        Value& userQuery = userQueries();
        if(it->hasQuery()) userQuery[Keys::query] = it->getQuery();
        if(it->hasCollection()) userQuery[Keys::collection] = it->getCollection();
        if(it->hasHitDocsNum()) userQuery[Keys::hit_docs_num] = it->getHitDocsNum();
        if(it->hasPageStart()) userQuery[Keys::page_start] = it->getPageStart();
        if(it->hasPageCount()) userQuery[Keys::page_count] = it->getPageCount();
        if(it->hasSessionId()) userQuery[Keys::session_id] = it->getSessionId();
        if(it->hasDuration()) userQuery[Keys::duration] = to_simple_string(it->getDuration());
        if(it->hasTimeStamp()) userQuery[Keys::timestamp] = to_simple_string(it->getTimeStamp());
    }
}

/**
 * @brief Action \b inject_user_queries.
 *
 * @section request
 *
 * The request format is identical with the response of \b user_queries. 
 *
 * - @b user_queries (@c Array): Array of queries with following fields
 *   - @b query* (@c String): keywords.
 *   - @b collection* (@c String): collection name.
 *   - @b hit_docs_num (@c Uint = 0): Number of hit documents.
 *   - @b page_start (@c Uint = 0): Page start index offset.
 *   - @b page_count (@c Uint = 0): Request number of documents in page.
 *   - @b duration (@c String = 0): Duration in format HH:MM:SS.fffffffff, where
 *        fffffffff is fractional seconds and can be omit.
 *   - @b timestamp (@c String = now): Time in format YYYY-mm-dd
 *     HH:MM:SS.fffffffff. If this is not specified, current time is used.
 * - @b conditions (@c Array): Result filtering conditions. See ConditionArrayParser.
 * - @b sort (@c Array): Sort result. See OrderArrayParser.
 *
 * @section response
 *
 * - @b user_queries All system events which fit conditions.
 *
 */
void LogAnalysisController::inject_user_queries()
{
    static const std::string session = "SESSION NOT USED";

    if (nullValue(request()[Keys::user_queries]))
    {
        return;
    }

    Value::ArrayType* queries = request()[Keys::user_queries].getArrayPtr();
    if (!queries)
    {
        response().addError(Keys::user_queries + " must be an array");
        return;
    }

    for (std::size_t i = 0; i < queries->size(); ++i)
    {
        Value& query = (*queries)[i];

        std::string keywords = asString(query[Keys::query]);
        std::string collection = asString(query[Keys::collection]);

        if (keywords.empty() || collection.empty())
        {
            response().addWarning("Require fields query and collection");
        }
        else
        {
            UserQuery queryLog;
            queryLog.setQuery(keywords);
            queryLog.setCollection(collection);
            queryLog.setHitDocsNum(asUint(query[Keys::hit_docs_num]));
            queryLog.setPageStart(asUint(query[Keys::page_start]));
            queryLog.setPageCount(asUint(query[Keys::page_count]));
            queryLog.setSessionId(session);

            // duration
            queryLog.setDuration(boost::posix_time::time_duration());
            try
            {
                if (query.hasKey(Keys::duration))
                {
                    queryLog.setDuration(
                        boost::posix_time::duration_from_string(
                            asString(query[Keys::duration])
                        )
                    );
                }
            }
            catch (const std::exception& e)
            {
                response().addWarning(
                    "Invalid duration: " + asString(query[Keys::duration])
                );
            }

            // timestamp
            // default is now
            queryLog.setTimeStamp(boost::posix_time::second_clock::local_time());
            try
            {
                if (query.hasKey(Keys::timestamp))
                {
                    queryLog.setTimeStamp(
                        boost::posix_time::time_from_string(
                            asString(query[Keys::timestamp])
                        )
                    );
                }
            }
            catch (const std::exception& e)
            {
                response().addWarning(
                    "Invalid timestamp: " + asString(query[Keys::timestamp])
                );
            }

            queryLog.save();
        }
    }
}

} // namespace sf1r