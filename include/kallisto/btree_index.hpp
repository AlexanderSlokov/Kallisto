#pragma once

#include <string>
#include <vector>
#include <memory>

namespace kallisto {

/**
 * Simplified B-Tree for strings (paths).
 * Acts as a validator before secret lookup in the CuckooTable.
 */
class BTreeIndex {
public:
	/**
	* @param degree Minimum degree (t). A node can have at most 2t-1 keys.
	* => Each node can has between 2 and 5 keys.
	*/
	BTreeIndex(int degree = 3);

	/**
	* Deep Copy Constructor
	*/
	BTreeIndex(const BTreeIndex& other);

	/**
	* Inserts a path into the index.
	* @param path The path to insert (e.g., "/prod/db").
	* @return true if insertion was successful.
	*/
	bool insertPath(const std::string& path);

	/**
	* Validates if a path exists in the index.
	* @param path The path to validate.
	* @return true if the path exists.
	*/
	bool validatePath(const std::string& path) const;

private:
	struct Node {
		bool is_leaf_node;
		std::vector<std::string> path_keys;
		std::vector<std::unique_ptr<Node>> child_nodes;

		Node(bool is_leaf = true) : is_leaf_node(is_leaf) {}

		// Deep Copy Constructor
		Node(const Node& other) : is_leaf_node(other.is_leaf_node), path_keys(other.path_keys) {
			for (const auto& child : other.child_nodes) {
				child_nodes.push_back(std::make_unique<Node>(*child));
			}
		}
	};

	std::unique_ptr<Node> root_node_;
	int min_degree_;

	/**
	* Splits a child node that is full into two separate nodes.
	* Maintains the B-Tree properties during insertion.
	* 
	* @param parent_node The parent node of the child being split.
	* @param child_index The index of the child to split in the parent's children array.
	* @param child_node The actual child node that is full and needs splitting.
	*/ 
	void splitChildNode(Node* parent_node, int child_index, Node* child_node);

	/**
	* Inserts a path key into a node that is guaranteed to not be full.
	* Recursively travels down the tree.
	* 
	* @param current_node The node to insert into.
	* @param path_key The path string to insert.
	*/ 
	void insertIntoNonFullNode(Node* current_node, const std::string& path_key);

	/**
	* Recursively searches for a path key in the B-Tree starting from a given node.
	* 
	* @param current_node The node to begin searching from.
	* @param path_key The path string to search for.
	* @return true if the path is found.
	*/ 
	bool containsPathRecursive(Node* current_node, const std::string& path_key) const;
};

} // namespace kallisto
