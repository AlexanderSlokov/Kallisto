#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

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
		bool is_leaf;
		std::vector<std::string> keys;
		std::vector<std::unique_ptr<Node>> children;

		Node(bool leaf = true) : is_leaf(leaf) {}

		// Deep Copy Constructor
		Node(const Node& other) : is_leaf(other.is_leaf), keys(other.keys) {
			for (const auto& child : other.children) {
				children.push_back(std::make_unique<Node>(*child));
			}
		}
	};

	std::unique_ptr<Node> root;
	int min_degree; // Fixed the cryptic "t"

	/**
	* Splits a child node into two nodes.
	* @param parent The parent node.
	* @param i The index of the child to split.
	* @param child The child node to split.
	*/ 
	void split_child(Node* parent, int i, Node* child);

	/**
	* Inserts a key into a non-full node.
	* @param node The node to insert into.
	* @param key The key to insert.
	*/ 
	void insert_non_full(Node* node, const std::string& key);

	/**
	* Searches for a key in the B-Tree.
	* @param node The node to search in.
	* @param key The key to search for.
	* @return true if the key is found.
	*/ 
	bool search(Node* node, const std::string& key) const;
};

} // namespace kallisto
