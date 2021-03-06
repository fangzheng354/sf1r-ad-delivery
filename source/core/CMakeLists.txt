INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/core/
  ${CMAKE_SOURCE_DIR}

  ${izenelib_INCLUDE_DIRS}
  ${wisekma_INCLUDE_DIRS}
  ${izenecma_INCLUDE_DIRS}
  ${izenejma_INCLUDE_DIRS}
  ${ilplib_INCLUDE_DIRS}
  ${idmlib_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${TokyoCabinet_INCLUDE_DIRS}
  ${Glog_INCLUDE_DIRS}
  ${Thrift_INCLUDE_DIR}
)

FILE(GLOB_RECURSE configuration_SRC "configuration-manager/*.cpp")
FILE(GLOB_RECURSE la_SRC "la-manager/*.cpp")
FILE(GLOB_RECURSE ranking_SRC "ranking-manager/*.cpp")
FILE(GLOB_RECURSE common_SRC "common/*.cpp")
FILE(GLOB_RECURSE log_SRC "log-manager/*.cpp")
FILE(GLOB_RECURSE directory_SRC "directory-manager/*.cpp")
FILE(GLOB_RECURSE search_SRC "search-manager/*.cpp")
FILE(GLOB_RECURSE query_SRC "query-manager/*.cpp")
FILE(GLOB_RECURSE aggregator_SRC "aggregator-manager/*.cpp")
FILE(GLOB_RECURSE node_SRC "node-manager/*.cpp")
FILE(GLOB sf1r_net_SRC "sf1r-net/*.cpp")
FILE(GLOB ad_SRC "ad-manager/*.cpp" "ad-manager/sponsored-ad-search/*.cpp")
FILE(GLOB slim_SRC "slim-manager/*.cpp")

FILE(GLOB index_SRC
  "index-manager/zambezi-manager/*.cpp"
  "index-manager/*.cpp"
  "index-manager/zambezi-tokenizer/*.cpp")

FILE(GLOB document_SRC
  "document-manager/*.cpp"
  "document-manager/snippet-generation-submanager/*.cpp"
  "document-manager/text-summarization-submanager/*.cpp"
  "document-manager/highlighter/*.cpp"
)

FILE(GLOB mining_SRC
  "mining-manager/*.cpp"
  "mining-manager/*.cc"
  "mining-manager/util/*.cpp"
  "mining-manager/group-manager/*.cpp"
  "mining-manager/attr-manager/*.cpp"
  "mining-manager/group-label-logger/*.cpp"
  "mining-manager/merchant-score-manager/*.cpp"
  "mining-manager/product-scorer/*.cpp"
  "mining-manager/product-score-manager/*.cpp"
  "mining-manager/product-ranker/*.cpp"
  "mining-manager/product-tokenizer/*.cpp"
  "mining-manager/custom-rank-manager/*.cpp"
  "mining-manager/suffix-match-manager/*.cpp"
)

FILE(GLOB laser_SRC
    "laser-manager/*.cpp"
    "laser-manager/predict/*.cpp"
    "laser-manager/service/*.cpp"
    "laser-manager/context/*.cpp"
    "laser-manager/clusteringmanager/clustering/*.cpp"
    "laser-manager/clusteringmanager/common/*.cpp"
    "laser-manager/clusteringmanager/type/*.cpp"
)

ADD_DEFINITIONS("-fno-strict-aliasing")
ADD_LIBRARY(sf1r_configuration_manager ${configuration_SRC})
ADD_LIBRARY(sf1r_la_manager ${la_SRC})
ADD_LIBRARY(sf1r_ranking_manager ${ranking_SRC})
ADD_LIBRARY(sf1r_common ${common_SRC})
ADD_LIBRARY(sf1r_index_manager ${index_SRC} )
ADD_LIBRARY(sf1r_log_manager ${log_SRC})
ADD_LIBRARY(sf1r_directory_manager ${directory_SRC})
ADD_LIBRARY(sf1r_query_manager ${query_SRC})
ADD_LIBRARY(sf1r_search_manager ${search_SRC})
ADD_LIBRARY(sf1r_mining_manager ${mining_SRC})
ADD_LIBRARY(sf1r_document_manager ${document_SRC})
ADD_LIBRARY(sf1r_aggregator_manager ${aggregator_SRC})
ADD_LIBRARY(sf1r_node_manager ${node_SRC})
ADD_LIBRARY(sf1r_net ${sf1r_net_SRC})
ADD_LIBRARY(sf1r_ad_manager ${ad_SRC})
ADD_LIBRARY(sf1r_laser_manager ${laser_SRC})
ADD_LIBRARY(sf1r_slim_manager ${slim_SRC})

INSTALL(TARGETS
    sf1r_configuration_manager
    sf1r_la_manager
    sf1r_ranking_manager
    sf1r_common
    sf1r_index_manager
    sf1r_log_manager
    sf1r_directory_manager
    sf1r_query_manager
    sf1r_search_manager
    sf1r_mining_manager
    sf1r_document_manager
    sf1r_aggregator_manager
    sf1r_node_manager
    sf1r_net
    sf1r_ad_manager
    sf1r_laser_manager
    sf1r_slim_manager
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT sf1r_libraries)
