/*
 * BK-tree implementation in C++
 * Copyright (C) 2012 Eiichi Sato
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BK_TREE_HPP_
#define _BK_TREE_HPP_

#include <map>
#include <cmath>
#include <vector>
#include <algorithm>

namespace qq
{

namespace detail
{

template<typename KeyType, typename MetricType, typename Distance>
class tree_node
{
private:
    typedef tree_node<KeyType, MetricType, Distance> NodeType;
    typedef std::map<MetricType, NodeType *> ChildType;

    std::vector<KeyType> values;
    ChildType* children;

    tree_node();
    tree_node(const tree_node&);
    tree_node& operator=(const tree_node&);


    MetricType clusterThreshold(int depth) const
    {
        // did a histogram of distances, to determine what might
        // be good cluster values. The avg distance between
        // hashes is 32, most fall within 28-39

        MetricType threshold[] = { 28, 29, 31, 35, 39 };

        // good for small sets
        //int depths[] = { 27, 30, 32 };

        if (depth >= sizeof(threshold)/sizeof(MetricType))
            return 65;

        return threshold[depth];
    }

    void _find_within(std::vector<std::pair<KeyType, MetricType>> &result,
            const KeyType &key, const MetricType d, int depth) const
    {
        Distance f;
        const KeyType& value = this->values[0];
        const MetricType n = f(key, value);

        //if (n <= d)
        //result.push_back(std::make_pair(value, n));

        if (n < clusterThreshold(depth))
            for (auto it = values.begin(); it != values.end(); ++it)
                if (f(key, *it) <= d)
                    result.push_back(std::make_pair(*it, n));

        if (!this->has_children())
            return;

        for (auto iter = children->begin(); iter != children->end(); ++iter)
        {
            MetricType distance = iter->first;
            if (n - d <= distance && distance <= n + d)
                iter->second->_find_within(result, key, d, depth+1);
        }
    }

public:

    std::vector<std::pair<KeyType, MetricType>> find_within(const KeyType &key,
            MetricType d) const
    {
        std::vector<std::pair<KeyType, MetricType>> result;
        _find_within(result, key, d, 0);
        return result;
    }

    tree_node(const KeyType &key)
    {
        children=NULL;
        values.push_back(key);
    }

    ~tree_node()
    {
        if (children)
        {
            for (auto iter = children->begin(); iter != children->end(); ++iter)
                delete iter->second;
            delete children;
        }
    }

    bool insert(NodeType *node, int depth)
    {
        if (!node)
            return false;

        Distance d;
        MetricType distance = d(node->values[0], this->values[0]);

        if (distance == 0)
            return false; /* value already exists */

        if (distance < clusterThreshold(depth))
        {
            values.push_back(node->values[0]);
            return true;
        }

        if (!children)
            children = new std::map<MetricType, NodeType *>();

        auto iterator = children->find(distance);
        if (iterator == children->end())
        {
            children->insert(std::make_pair(distance, node));
            return true;
        }

        return iterator->second->insert(node, depth+1);
    }

    bool has_children() const
    {
        return children && children->size() > 0;
    }

    void dump_tree(int depth = 0)
    {
        printf("%d(c=%d t=%d):", depth, (children ? children->size() : 0), clusterThreshold(depth));

        for (int i = 0; i < depth; ++i)
            std::cout << "    ";
        std::cout << this->values.size() << std::endl;
        if (this->has_children())
            for (auto iter = children->begin(); iter != children->end(); ++iter)
                iter->second->dump_tree(depth + 1);
    }
};

template<typename KeyType, typename MetricType>
struct default_distance
{
    MetricType operator()(const KeyType &ki, const KeyType &kj)
    {
        return sqrt((ki - kj) * (ki - kj));
    }
};

} /* namespace detail */

template<typename KeyType, typename MetricType = double,
        typename Distance = detail::default_distance<KeyType, MetricType> >
class bktree
{
private:
    typedef detail::tree_node<KeyType, MetricType, Distance> NodeType;

private:
    NodeType *m_top;
    size_t m_n_nodes;

public:
    bktree() :
            m_top(NULL), m_n_nodes(0)
    {
        printf("bktree: sizeof tree_node=%lu\n", sizeof(NodeType));
    }

public:
    void insert(const KeyType &key)
    {
        NodeType *node = new NodeType(key);
        if (!m_top)
        {
            m_top = node;
            m_n_nodes = 1;
            return;
        }
        if (m_top->insert(node, 0))
            ++m_n_nodes;
    }
    ;

//    void cluster()
//    {
//        if (m_top)
//            m_top->cluster(0);
//    }

public:
    std::vector<std::pair<KeyType, MetricType>> find_within(KeyType key,
            MetricType d) const
    {
        return m_top->find_within(key, d);
    }

    void dump_tree()
    {
        m_top->dump_tree();
    }

public:
    size_t size() const
    {
        return m_n_nodes;
    }
};

} /* namespace qq */

#endif /* _BK_TREE_HPP_ */
