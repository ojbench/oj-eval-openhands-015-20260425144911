#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace std;

const int MAX_KEY_LEN = 64;
const int M = 50;

struct Key {
    char index[MAX_KEY_LEN + 1];
    int value;

    Key() {
        memset(index, 0, sizeof(index));
        value = 0;
    }

    Key(const char* idx, int v) {
        strncpy(index, idx, MAX_KEY_LEN);
        index[MAX_KEY_LEN] = '\0';
        value = v;
    }

    bool operator<(const Key& other) const {
        int cmp = strcmp(index, other.index);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }

    bool operator==(const Key& other) const {
        return value == other.value && strcmp(index, other.index) == 0;
    }

    bool operator<=(const Key& other) const {
        return *this < other || *this == other;
    }
};

struct Node {
    bool is_leaf;
    int size;
    long parent;
    long next;
    Key keys[M];
    long children[M + 1];

    Node() {
        is_leaf = true;
        size = 0;
        parent = -1;
        next = -1;
        for (int i = 0; i < M + 1; ++i) children[i] = -1;
    }
};

class BPlusTree {
private:
    string filename;
    fstream file;
    long root;
    long free_list;
    long end_of_file;

    void read_node(long offset, Node& node) {
        if (offset == -1) return;
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));
    }

    void write_node(long offset, const Node& node) {
        if (offset == -1) return;
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
    }

    long alloc_node() {
        long offset;
        if (free_list != -1) {
            offset = free_list;
            Node node;
            read_node(offset, node);
            free_list = node.next;
        } else {
            offset = end_of_file;
            end_of_file += sizeof(Node);
        }
        return offset;
    }

    void dealloc_node(long offset) {
        Node node;
        read_node(offset, node);
        node.next = free_list;
        write_node(offset, node);
        free_list = offset;
    }

    void update_header() {
        file.seekp(0);
        file.write(reinterpret_cast<char*>(&root), sizeof(long));
        file.write(reinterpret_cast<char*>(&free_list), sizeof(long));
        file.write(reinterpret_cast<char*>(&end_of_file), sizeof(long));
    }

public:
    BPlusTree(string name) : filename(name) {
        file.open(filename, ios::in | ios::out | ios::binary);
        if (!file) {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            root = -1;
            free_list = -1;
            end_of_file = 3 * sizeof(long);
            update_header();
        } else {
            file.seekg(0);
            file.read(reinterpret_cast<char*>(&root), sizeof(long));
            file.read(reinterpret_cast<char*>(&free_list), sizeof(long));
            file.read(reinterpret_cast<char*>(&end_of_file), sizeof(long));
        }
    }

    ~BPlusTree() {
        update_header();
        file.close();
    }

    void insert(const Key& key) {
        if (root == -1) {
            root = alloc_node();
            Node node;
            node.is_leaf = true;
            node.size = 1;
            node.keys[0] = key;
            write_node(root, node);
            update_header();
            return;
        }

        long curr_offset = root;
        Node curr;
        while (true) {
            read_node(curr_offset, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.size && !(key < curr.keys[i])) i++;
            curr_offset = curr.children[i];
        }

        // Check for duplicate
        for (int i = 0; i < curr.size; ++i) {
            if (curr.keys[i] == key) return;
        }

        // Insert into leaf
        int i = curr.size - 1;
        while (i >= 0 && key < curr.keys[i]) {
            curr.keys[i + 1] = curr.keys[i];
            i--;
        }
        curr.keys[i + 1] = key;
        curr.size++;
        write_node(curr_offset, curr);

        if (curr.size == M) {
            split_leaf(curr_offset, curr);
        }
    }

    void split_leaf(long offset, Node& node) {
        long new_offset = alloc_node();
        Node new_node;
        new_node.is_leaf = true;
        new_node.parent = node.parent;
        new_node.next = node.next;
        node.next = new_offset;

        int mid = M / 2;
        new_node.size = M - mid;
        for (int i = 0; i < new_node.size; ++i) {
            new_node.keys[i] = node.keys[mid + i];
        }
        node.size = mid;

        write_node(offset, node);
        write_node(new_offset, new_node);

        insert_into_parent(offset, new_node.keys[0], new_offset);
    }

    void insert_into_parent(long left_offset, const Key& key, long right_offset) {
        Node left;
        read_node(left_offset, left);
        if (left_offset == root) {
            root = alloc_node();
            Node new_root;
            new_root.is_leaf = false;
            new_root.size = 1;
            new_root.keys[0] = key;
            new_root.children[0] = left_offset;
            new_root.children[1] = right_offset;
            write_node(root, new_root);
            left.parent = root;
            write_node(left_offset, left);
            Node right;
            read_node(right_offset, right);
            right.parent = root;
            write_node(right_offset, right);
            update_header();
            return;
        }

        long parent_offset = left.parent;
        Node parent;
        read_node(parent_offset, parent);

        int i = parent.size - 1;
        while (i >= 0 && key < parent.keys[i]) {
            parent.keys[i + 1] = parent.keys[i];
            parent.children[i + 2] = parent.children[i + 1];
            i--;
        }
        parent.keys[i + 1] = key;
        parent.children[i + 2] = right_offset;
        parent.size++;
        write_node(parent_offset, parent);

        Node right;
        read_node(right_offset, right);
        right.parent = parent_offset;
        write_node(right_offset, right);

        if (parent.size == M) {
            split_internal(parent_offset, parent);
        }
    }

    void split_internal(long offset, Node& node) {
        long new_offset = alloc_node();
        Node new_node;
        new_node.is_leaf = false;
        new_node.parent = node.parent;

        int mid = M / 2;
        Key up_key = node.keys[mid];

        new_node.size = M - mid - 1;
        for (int i = 0; i < new_node.size; ++i) {
            new_node.keys[i] = node.keys[mid + 1 + i];
            new_node.children[i] = node.children[mid + 1 + i];
        }
        new_node.children[new_node.size] = node.children[M];
        node.size = mid;

        write_node(offset, node);
        write_node(new_offset, new_node);

        for (int i = 0; i <= new_node.size; ++i) {
            Node child;
            read_node(new_node.children[i], child);
            child.parent = new_offset;
            write_node(new_node.children[i], child);
        }

        insert_into_parent(offset, up_key, new_offset);
    }

    void remove(const Key& key) {
        if (root == -1) return;

        long curr_offset = root;
        Node curr;
        while (true) {
            read_node(curr_offset, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.size && !(key < curr.keys[i])) i++;
            curr_offset = curr.children[i];
        }

        int i = 0;
        while (i < curr.size && !(key == curr.keys[i])) i++;
        if (i == curr.size) return; // Not found

        for (int j = i; j < curr.size - 1; ++j) {
            curr.keys[j] = curr.keys[j + 1];
        }
        curr.size--;
        write_node(curr_offset, curr);
        // For simplicity, we don't merge nodes on deletion as disk space is plenty
        // and it's not required by the problem.
    }

    void find(const char* index) {
        if (root == -1) {
            cout << "null" << endl;
            return;
        }

        long curr_offset = root;
        Node curr;
        Key search_key(index, -1);
        while (true) {
            read_node(curr_offset, curr);
            if (curr.is_leaf) break;
            int i = 0;
            while (i < curr.size && !(search_key < curr.keys[i])) i++;
            curr_offset = curr.children[i];
        }

        bool found = false;
        bool first = true;
        while (curr_offset != -1) {
            read_node(curr_offset, curr);
            for (int i = 0; i < curr.size; ++i) {
                if (strcmp(curr.keys[i].index, index) == 0) {
                    if (!first) cout << " ";
                    cout << curr.keys[i].value;
                    found = true;
                    first = false;
                } else if (strcmp(curr.keys[i].index, index) > 0) {
                    goto end_find;
                }
            }
            curr_offset = curr.next;
        }

    end_find:
        if (!found) cout << "null";
        cout << endl;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPlusTree tree("database.db");

    int n;
    if (!(cin >> n)) return 0;

    while (n--) {
        string cmd;
        cin >> cmd;
        if (cmd == "insert") {
            char index[MAX_KEY_LEN + 1];
            int value;
            cin >> index >> value;
            tree.insert(Key(index, value));
        } else if (cmd == "delete") {
            char index[MAX_KEY_LEN + 1];
            int value;
            cin >> index >> value;
            tree.remove(Key(index, value));
        } else if (cmd == "find") {
            char index[MAX_KEY_LEN + 1];
            cin >> index;
            tree.find(index);
        }
    }

    return 0;
}
