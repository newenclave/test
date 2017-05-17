#include <iostream>
#include <cstdint>
#include <memory>
#include <array>
#include <vector>
#include <algorithm>

#include "etool/dumper/dump.h"
#include "etool/details/operators.h"

#include "dyn_array.h"

using namespace etool;

namespace {

template <typename T, typename Less = std::less<T> >
std::size_t lower_bound( const T *arr, std::size_t length,
                         const T &val, Less less )
{
    std::size_t next   = 0;
    while( next < length ) {
        std::size_t middle = next + ( ( length - next ) >> 1 );
        if( less(arr[middle], val) ) {
            next = middle + 1;
        } else {
            length = middle;
        }
    }
    return next;
}


template <typename T, typename Less = std::less<T> >
std::size_t upper_bound( const T *arr, std::size_t length,
                         const T &val, Less less )
{
    size_t next   = 0;
    while( next < length ) {
        size_t middle = next + ( ( length - next ) >> 1 );
        if( less(val, arr[middle]) ) {
            length = middle;
        } else {
            next = middle + 1;
        }
    }
    return next;
}

template <typename T, std::size_t NodeMax, typename Less = std::less<T> >
struct btree {

    static_assert( ((NodeMax & 1) == 1) && (NodeMax > 2),
                   "Count must be odd and greater then 2" );

    static const std::size_t maximum = NodeMax;
    static const std::size_t middle  = maximum / 2;

    using value_type = T;

    btree( )
        :root_(new bnode)
    { }

    struct cmp {

        static
        bool eq( const value_type &l, const value_type &r )
        {
            using op = details::operators::cmp<T, Less>;
            return op::equal(l, r);
        }

        bool operator ( ) ( const value_type &l, const value_type &r ) const
        {
            using op = details::operators::cmp<T, Less>;
            return op::less(l, r);
        }
    };

    struct bnode {

        using ptr_type      = std::unique_ptr<bnode>;
        using value_array   = dyn_array<value_type, NodeMax>;
        using pointer_array = dyn_array<ptr_type, NodeMax + 1>;

        bnode( )
        {
            //values_.fill(value_type( ));
        }

        bnode( const bnode& other ) = delete;
        void operator = ( const bnode& ) = delete;

        bnode( bnode &&other )
            :values_(std::move(other.values_))
            ,next_(std::move(other.next_))
        { }

        bnode &operator = ( bnode &&other )
        {
            values_ = std::move(other.values_);
            next_   = std::move(other.next_);
        }

        void erase( const value_type &val )
        {
            auto node = node_with( val );
            if( node.first ) {
                auto n = node.first;
                auto p = node.second;
                if( n->is_leaf( ) ) {
                    n->values_.erase( n->values_.begin( ) + p );
                } else {
                    auto val = std::move(n->next_[p + 1]->values_[0]);
                    n->next_[p + 1]->values_.erase(n->next_[p + 1]->values_.begin( ));
                    n->values_[p] = std::move(val);
                }
            }
        }

        void insert( value_type val )
        {
            if( values_.empty( ) ) {
                values_.emplace(values_.begin( ), std::move(val));
            } else {

                auto pos = lower_bound( &values_[0], values_.size( ),
                                         val, cmp( ) );

                if( pos != values_.size( ) && cmp::eq(values_[pos], val ) ) {
                    return;
                }

                if( is_leaf(  ) ) {

                    values_.insert( values_.begin( ) + pos,
                                    std::move(val) );

                } else {
                    next_[pos]->insert(std::move(val));

                    if( next_[pos]->full( ) ) {

                        val = std::move(next_[pos]->values_[middle]);

                        auto pair = split(std::move(next_[pos]));

                        pair.first->parent_  = this;
                        pair.second->parent_ = this;

                        next_[pos] = std::move(pair.first);

                        next_.emplace( next_.begin( ) + pos + 1,
                                       std::move(pair.second) );

                        values_.emplace( values_.begin( ) + pos,
                                         std::move(val) );

                    }
                }
            }
        }

        bool full( ) const
        {
            return values_.full( );
        }

        static
        std::pair<ptr_type, ptr_type> split( ptr_type src )
        {
            ptr_type right(new bnode);

            right->values_.assign( src->values_.begin( ) + middle + 1,
                                   src->values_.end( ) );

            if(!src->next_.empty( ) ) {

                right->next_.assign_move( src->next_.begin( ) + middle + 1,
                                          src->next_.end( ) );

                for( std::size_t i = middle + 1; i < maximum + 1; ++i ) {
                    src->next_[i].reset( );
                }

                src->next_.reduce(middle + 1);
            }

            src->values_.reduce(middle + 1);

            return std::make_pair(std::move(src), std::move(right));
        }

        bool is_leaf( ) const
        {
            return next_.empty( );
        }

        std::pair<bnode *, std::size_t> node_with( const value_type &val )
        {
            auto pos = lower_bound( &values_[0], values_.size( ),
                                     val, cmp( ) );

            if( pos != values_.size( ) && cmp::eq(values_[pos], val) ) {
                return std::make_pair(this, pos);
            }

            if( !is_leaf( ) ) {
                return next_[pos]->node_with(val);
            }

            return std::make_pair(nullptr, 0);
        }

        bnode *left_sibling( )
        {
            if( parent_ && (this != parent_->next_[0].get( ) ) ) {
                for( std::size_t i = 1; i < parent_->next_.size( ); ++i ) {
                    if( parent_->next_[i].get( ) == this ) {
                        return parent_->next_[i - 1].get( );
                    }
                }
            }
            return nullptr;
        }

        bnode *right_sibling( )
        {

            if( parent_ ) {
                auto s = parent_->next_.size( ) - 1;
                if( parent_->next_[s] != this ) { /// last one

                    for( std::size_t i = s; i > 0; --i ) {
                        if( parent_->next_[i - 1].get( ) == this ) {
                            return parent_->next_[i].get( );
                        }
                    }
                }
            }

            return nullptr;
        }

        bnode *parent_ = nullptr;
        value_array   values_;
        pointer_array next_;
    };

    void insert( value_type val )
    {
        root_->insert( std::move(val) );

        if( root_->full( ) ) {
            std::unique_ptr<bnode> new_root(new bnode);
            auto val = std::move(root_->values_[middle]);
            auto pair = bnode::split(std::move(root_));

            pair.first->parent_  = new_root.get( );
            pair.second->parent_ = new_root.get( );

            new_root->values_.insert( new_root->values_.begin( ),
                                      std::move(val) );
            new_root->next_.insert( new_root->next_.end( ),
                                    std::move(pair.first) );

            new_root->next_.insert( new_root->next_.end( ),
                                    std::move(pair.second) );
            root_.swap(new_root);

        }

    }

    std::unique_ptr<bnode> root_;
};

template <typename A>
void print( const A &a )
{
    for( auto &v: a ) {
        std::cout << " " << v;
    }
    std::cout << "\ntotal: " << a.size( ) << "\n";
}

}

int main( )
{
    srand(time(nullptr));

    using btree_type = btree<int, 17>;
    btree_type bt;

    for( auto i=0; i<2100; i++ ) {
        bt.insert( i );
    }

    bt.root_->erase( 44 );

    auto nw = bt.root_->node_with( 13 );
    //auto nw = bt.root_->node_with( random() % 2100 );

    if( nw.first ) {
        print( nw.first->values_ );
        std::cout << nw.second << "\n";
    }

    return 0;
}
