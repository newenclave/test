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

    static_assert( NodeMax > 2, "Count must be greater then 2" );

    static const std::size_t maximum = NodeMax;
    static const std::size_t middle  = maximum / 2;
    static const std::size_t odd     = maximum % 2;
    static const std::size_t minimum = middle  + odd - 1;

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
        using value_array   = dyn_array<value_type, maximum>;
        using pointer_array = dyn_array<ptr_type,   maximum + 1>;

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

        void remove_from_leaf( const value_type &val, std::size_t pos )
        {
            auto sb    = siblings( val );
            auto left  = sb.first;
            auto right = sb.second;

            values_.erase_pos( pos );

            if( less_minimum( ) && parent_ ) {

                auto ls = left  ? left->size( )  : 0;
                auto rs = right ? right->size( ) : 0;

                auto pp = parent_->lower_of( val );

                if( (rs > ls) && right->has_donor( ) ) {

                    /// rotate right
                    values_.push_back( std::move(parent_->values_[pp]) );
                    parent_->values_[pp] = std::move(right->values_[0]);
                    right->values_.erase_pos( 0 );
                } else if( left && left->has_donor( ) ) {

                    /// rotate left
                    values_.push_back( std::move(parent_->values_[pp - 1]) );
                    parent_->values_[pp - 1] = std::move(left->last( ));
                    left->values_.reduce(1);
                } else {
                    /// merging
                }
            }

        }

        void remove_from_node( const value_type & /*val*/, std::size_t pos )
        {
            auto left  = next_[pos].get( );
            auto right = next_[pos + 1].get( );

            auto ls = left->size( ) ;
            auto rs = right->size( );

            if( right->is_leaf( ) ) {
                if( (rs > ls) && right->has_donor( ) ) {
                    values_[pos] = std::move(right->values_[0]);
                    right->values_.erase_pos( 0 );
                } else {
                    values_[pos] = std::move(left->last( ));
                    left->values_.reduce(1);
                }
            }
        }

        void erase_fix( const value_type &val, std::size_t pos )
        {
            if( is_leaf( ) ) {
                remove_from_leaf( val, pos );
            } else {
                remove_from_node( val, pos );
            }
        }

        value_type &last( )
        {
            return values_[size( ) - 1];
        }

        std::size_t size( ) const
        {
            return values_.size( );
        }

        bool less_minimum( ) const
        {
            return (size( ) < minimum);
        }

        bool has_donor( ) const
        {
            return (size( ) > minimum);
        }

        void erase( const value_type &val )
        {
            auto node = node_with( val );
            if( node.first ) {
                node.first->erase_fix( val, node.second );
            }
        }

        bool full( ) const
        {
            return values_.full( );
        }

        bool is_leaf( ) const
        {
            return next_.empty( );
        }

        std::size_t lower_of( const value_type &val ) const
        {
            return lower_bound( &values_[0], values_.size( ), val, cmp( ) );
        }

        std::size_t upper_of( const value_type &val ) const
        {
            return upper_bound( &values_[0], values_.size( ), val, cmp( ) );
        }

        void insert( value_type val )
        {
            if( values_.empty( ) ) {
                values_.emplace(values_.begin( ), std::move(val));
            } else {

                auto pos = lower_of( val );

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

        static
        std::pair<ptr_type, ptr_type> split( ptr_type src )
        {
            static const std::size_t splitter = middle + odd;

            ptr_type right(new bnode);

            right->values_.assign( src->values_.begin( ) + (middle + 1),
                                   src->values_.end( ) );

            if(!src->next_.empty( ) ) {

                right->next_.assign_move( src->next_.begin( ) + (middle + 1),
                                          src->next_.end( ) );

                for( std::size_t i = splitter; i < maximum + 1; ++i ) {
                    src->next_[i].reset( );
                }

                src->next_.reduce(splitter);
            }

            src->values_.reduce(splitter);

            return std::make_pair(std::move(src), std::move(right));
        }

        std::pair<bnode *, std::size_t> node_with( const value_type &val )
        {
            auto pos = lower_of( val );

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
            if( parent_ ) {
                auto my_pos = parent_->lower_of( values_[0] );
                if( my_pos > 0 ) {
                    return parent_->next_[my_pos - 1].get( );
                }
            }
            return nullptr;
        }

        bnode *right_sibling( )
        {
            if( parent_ ) {
                auto my_pos = parent_->lower_of( values_[0] );
                if( my_pos < maximum ) {
                    return parent_->next_[my_pos + 1].get( );
                }
            }
            return nullptr;
        }

        std::pair<bnode *, bnode *> siblings( )
        {
            return siblings( values_[0] );
        }

        std::pair<bnode *, bnode *> siblings( const value_type &val )
        {
            if( parent_ ) {

                bnode *left = nullptr;
                bnode *right = nullptr;

                auto my_pos = parent_->lower_of( val );

                if( my_pos > 0 ) {
                    left = parent_->next_[my_pos - 1].get( );
                }

                if( my_pos < parent_->size( ) ) {
                    right = parent_->next_[my_pos + 1].get( );
                }

                return std::make_pair( left, right );

            }
            return std::make_pair(nullptr, nullptr);
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

            val       = std::move(root_->values_[middle]);
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

std::size_t minim( std::size_t v )
{
    return (v / 2) + (v % 2) - 1;
}

}

int main( )
{
    srand(time(nullptr));

    using btree_type = btree<int, 3>;
    btree_type bt;

//    for( auto i=0; i<100; i++ ) {
//        bt.insert( i );
//    }

    bt.insert( 20 );
    bt.insert( 10 );
    bt.insert( 30 );
    bt.insert( 15 );
    bt.insert( 5 );
    bt.insert( 7 );
    bt.insert( 26 );
    bt.insert( 35 );

    bt.root_->erase( 15 );

    auto nw = bt.root_->node_with( 3 );
    //auto nw = bt.root_->node_with( random() % 2100 );



    if( nw.first ) {

        auto sb = nw.first->siblings( );

        print( nw.first->values_ );
        std::cout << nw.second << "\n";
    }

    return 0;
}
