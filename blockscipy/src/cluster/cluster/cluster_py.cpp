//
//  cluster_py.cpp
//  blocksci
//
//  Created by Harry Kalodner on 1/14/17.
//  Copyright © 2017 Harry Kalodner. All rights reserved.
//

#include "cluster_py.hpp"
#include "ranges_py.hpp"
#include "caster_py.hpp"

#include <blocksci/cluster/cluster_manager.hpp>
#include <blocksci/heuristics/change_address.hpp>

#include <blocksci/chain/blockchain.hpp>
#include <blocksci/chain/block.hpp>
#include <blocksci/cluster/cluster.hpp>

#include <range/v3/range_for.hpp>

#include <pybind11/iostream.h>
#include <pybind11/operators.h>

namespace py = pybind11;
using namespace blocksci;

int64_t totalOutWithoutSelfChurn(const blocksci::Block &block, blocksci::ClusterManager &manager) {
    int64_t total = 0;
    RANGES_FOR(auto tx, block) {
        std::set<uint32_t> inputClusters;
        RANGES_FOR(auto input, tx.inputs()) {
            auto cluster = manager.getCluster(input.getAddress());
            if (cluster.getTypeEquivSize() < 30000) {
                inputClusters.insert(cluster.clusterNum);
            }
        }
        RANGES_FOR(auto output, tx.outputs()) {
            if ((!output.isSpent() || output.getSpendingTx()->getBlockHeight() - block.height() > 3) && inputClusters.find(manager.getCluster(output.getAddress()).clusterNum) == inputClusters.end()) {
                total += output.getValue();
            }
        }
    }
    return total;
}

void init_cluster_manager(pybind11::module &s) {
    s
    .def("total_without_self_churn", totalOutWithoutSelfChurn)
    ;

    
    py::class_<ClusterManager>(s, "ClusterManager", "Class managing the cluster dat")
    .def(py::init([](std::string arg, blocksci::Blockchain &chain) {
       return ClusterManager(arg, chain.getAccess());
    }))
    .def_static("create_clustering", [](const std::string &location, Blockchain &chain, BlockHeight start, BlockHeight stop, Proxy<ranges::optional<Output>> &heuristic, bool shouldOverwrite) {
        py::scoped_ostream_redirect stream(
                                           std::cout,
                                           py::module::import("sys").attr("stdout")
                                           );
        if (stop == -1) {
            stop = chain.size();
        }
        auto range = chain[{start, stop}];
        std::function<ranges::optional<Output>(const Transaction &tx)> changeFunc = [heuristic](const Transaction &tx) {
            return heuristic(tx);
        };
        return ClusterManager::createClustering(range, changeFunc, location, shouldOverwrite);
    }, py::arg("location"), py::arg("chain"), py::arg("start") = 0, py::arg("stop") = -1,
    py::arg("heuristic") = Proxy<ranges::optional<Output>>{std::function<ranges::optional<Output>(std::any &)>{[](std::any &v) -> ranges::optional<Output> {
            return heuristics::ChangeHeuristic{heuristics::LegacyChange{}}.uniqueChange(std::any_cast<Transaction>(v));
        }}}, py::arg("should_overwrite") = false)
    .def("cluster_with_address", [](const ClusterManager &cm, const Address &address) -> Cluster {
       return cm.getCluster(address);
    }, py::arg("address"), "Return the cluster containing the given address")
    .def("clusters", [](ClusterManager &cm) -> Range<Cluster> {
        return {cm.getClusters()};
    }, "Get a list of all clusters (The list is lazy so there is no cost to calling this method)")
    .def("tagged_clusters", [](ClusterManager &cm, const std::unordered_map<blocksci::Address, std::string> &tags) -> Iterator<TaggedCluster> {
        return cm.taggedClusters(tags);
    }, py::arg("tagged_addresses"), "Given a dictionary of tags, return a list of TaggedCluster objects for any clusters containing tagged scripts")
    ;
}

void init_cluster(py::class_<Cluster> &cl) {
    cl
    .def_readonly("cluster_num", &Cluster::clusterNum)
    .def("__len__", &Cluster::getSize)
    .def("__hash__", [] (const Cluster &cluster) {
        return cluster.clusterNum;
    })
    .def("txes", &Cluster::getTransactions, "Returns a list of all transactions involving this cluster")
    .def("in_txes",&Cluster::getInputTransactions, "Returns a list of all transaction where this cluster was an input")
    .def("out_txes", &Cluster::getOutputTransactions, "Returns a list of all transaction where this cluster was an output")
    ;
}

void addClusterRangeMethods(RangeClasses<Cluster> &classes) {
    addAllRangeMethods(classes);
}
