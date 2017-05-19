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

    static_assert( NodeMax > 2, "Maximum must be at least 3" );

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

        value_type &last( )
        {
            return values_.back( );
        }

        value_type &first( )
        {
            return values_.front( );
        }

        std::size_t size( ) const
        {
            return values_.size( );
        }

        bool empty( ) const
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
                node.first->erase_fix( node.second );
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

        static
        void rotate_cw( bnode *node, std::size_t pos ) // clock wise
        {
            auto l = node->next_[pos].get( );
            auto r = node->next_[pos + 1].get( );

            r->values_.push_front(std::move(node->values_[pos]));
            node->values_[pos] = std::move(l->last( ));

            if( !l->is_leaf( ) ) {
                r->next_.push_front( std::move( l->next_[l->size( )] ) );
                r->next_.front( )->parent_ = r;
                l->next_.reduce( 1 );
            }
            l->values_.reduce( 1 );
        }

        static
        void rotate_ccw( bnode *node, std::size_t pos ) // contra clock wise
        {
            auto l = node->next_[pos].get( );
            auto r = node->next_[pos + 1].get( );

            l->values_.push_back(std::move(node->values_[pos]));
            node->values_[pos] = std::move(r->first( ));

            if( !l->is_leaf( ) ) {
                r->next_[0]->parent_ = l;
                l->next_.push_back( std::move( r->next_[0] ) );
                r->next_.erase_pos( 0 );
            }

            r->values_.erase_pos( 0 );
        }

        static
        void merge( bnode *node, std::size_t pos )
        {
            auto l = node->next_[pos].get( );
            auto r = std::move(node->next_[pos + 1]);

            l->values_.push_back( std::move(node->values_[pos]) );

            node->values_.erase_pos(pos);

            node->next_[pos + 1] = std::move( node->next_[pos] );
            node->next_.erase_pos(pos);

            for( auto &v: r->values_ ) {
                l->values_.push_back( std::move(v) );
            }

            for( auto &p: r->next_ ) {
                p->parent_ = l;
                l->next_.push_back( std::move(p) );
            }

            if( node->empty( ) && node->parent_ ) {
                node->fix_me( );
            }
        }

        void fix_me( )
        {
            auto sb = siblings( );

            if( sb.first && sb.first->has_donor( ) ) {

                rotate_cw( parent_, my_position( ) - 1 );

            } else if( sb.second && sb.second->has_donor( ) ) {

                rotate_ccw( parent_, my_position( ) );

            } else {

                auto pp = my_position( );

                if( sb.first ) {
                    merge( parent_, pp - 1 );
                } else {
                    merge( parent_, pp );
                }
            }
        }

        void remove_from_leaf( std::size_t pos )
        {

            values_.erase_pos( pos );

            if( empty( ) && parent_ ) {
                fix_me( );
            }
        }

        void remove_from_node( std::size_t pos )
        {
            auto ml = most_left( next_[pos].get( ) );
            values_[pos] = std::move(ml->last( ));
            ml->values_.reduce( 1 ); /// leaf! doesn't have children
            if( ml->empty( ) ) {
                ml->fix_me( );
            }
        }

        void erase_fix( std::size_t pos )
        {
            if( is_leaf( ) ) {
                remove_from_leaf( pos );
            } else {
                remove_from_node( pos );
            }
        }

        bnode *next_left( )
        {
            if( size( ) > 0 ) {
                return next_.front( ).get( );
            }
            return nullptr;
        }

        bnode *next_right( )
        {
            if( size( ) > 0 ) {
                return next_[size( )].get( );
            }
            return nullptr;
        }

        void insert( value_type val )
        {
            if( values_.empty( ) ) {

                values_.push_back( std::move(val) );

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

                    //auto nnn = next_[pos].get( );

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

                for( std::size_t i = middle + 1; i < src->next_.size( ); ++i ) {
                    src->next_[i]->parent_ = right.get( );
                    right->next_.push_back( std::move(src->next_[i]));
                }

//                for( std::size_t i = splitter; i < maximum + 1; ++i ) {
//                    src->next_[i].reset( );
//                }

                src->next_.reduce(splitter);
            }

            src->values_.reduce(splitter);

            return std::make_pair(std::move(src), std::move(right));
        }


        std::pair<bnode *, std::size_t> node_with_rec( const value_type &val )
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

        std::pair<bnode *, std::size_t> node_with( const value_type &val )
        {

            auto next = this ;
            while( next ) {

                auto pos = next->lower_of( val );

                if( pos != next->values_.size( ) &&
                    cmp::eq(next->values_[pos], val) )
                {
                    return std::make_pair(next, pos);
                }

                next = next->next_[pos].get( );
            }
            return std::make_pair(nullptr, 0);
        }

        static
        bnode *most_left( bnode *node )
        {
            while( node ) {
                if( node->next_[0] ) {
                    node = node->next_[0].get( );
                } else {
                    break;
                }
            }
            return node;
        }

        static
        bnode *most_right( bnode *node )
        {
            while( node ) {
                if( node->next_[node->size( )] ) {
                    node = node->next_[node->size( )].get( );
                } else {
                    break;
                }
            }
            return node;
        }

        std::pair<bnode *, bnode *> siblings_by_pos( size_t my_pos )
        {
            bnode *left  = nullptr;
            bnode *right = nullptr;

            if( my_pos > 0 ) {
                left = parent_->next_[my_pos - 1].get( );
            }

            if( my_pos < parent_->size( ) ) {
                right = parent_->next_[my_pos + 1].get( );
            }

            return std::make_pair( left, right );
        }

        std::size_t my_position( ) const
        {
            if(values_.empty( )) {
                size_t my_pos = 0;

                for( ;my_pos < parent_->next_.size( ); ++my_pos ) {
                    if( parent_->next_[my_pos].get( ) == this ) {
                        break;
                    }
                }

                return my_pos;
            } else {
                return parent_->lower_of( values_[0] );
            }
        }

        std::pair<bnode *, bnode *> siblings( )
        {
            if( values_.empty( ) ) {
                size_t my_pos = my_position( );
                return siblings_by_pos( my_pos );

            } else {
                return siblings( values_[0] );
            }
        }

        std::pair<bnode *, bnode *> siblings( const value_type &val )
        {
            if( parent_ ) {
                auto my_pos = parent_->lower_of( val );
                return siblings_by_pos( my_pos );
            }
            return std::make_pair(nullptr, nullptr);
        }

        template <typename Call>
        void for_each_impl( bnode *node, Call &call )
        {
            std::size_t i = 0;
            if( node->is_leaf( ) ) {
                for( ; i<node->values_.size( ); i++ ) {
                    call( node->values_[i] );
                }
            } else {
                for( ; i<node->values_.size( ); i++ ) {
                    for_each_impl(node->next_[i].get( ), call);
                    call( node->values_[i] );
                }
                for_each_impl(node->next_[i].get( ), call);
            }
        }

        template <typename Call>
        void for_each( Call call )
        {
            for_each_impl(this, call);
        }

        bnode *parent_ = nullptr;
        value_array   values_;
        pointer_array next_;
    };

    void erase( const value_type &val )
    {
        root_->erase( val );
        if( root_->values_.empty( ) && !root_->next_.empty( ) ) {
            auto tmp = std::move(root_->next_[0]);
            tmp->parent_ = nullptr;
            root_ = std::move(tmp);
        }
    }

    void insert( value_type val )
    {
        root_->insert( std::move(val) );

        if( root_->full( ) ) {
            std::unique_ptr<bnode> new_root(new bnode);

            val       = std::move(root_->values_[middle]);
            auto pair = bnode::split(std::move(root_));

            pair.first->parent_  = new_root.get( );
            pair.second->parent_ = new_root.get( );

            new_root->values_.push_back( std::move(val) );
            new_root->next_.push_back( std::move(pair.first) );
            new_root->next_.push_back( std::move(pair.second) );

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

    auto maxx = 200;

    srand(time(nullptr));

    using btree_type = btree<int, 64>;
    btree_type bt;

    for( auto i=1; i<=maxx; i++ ) {
        bt.insert( rand( ) %1000 );
        //bt.insert( i );
    }

    bt.root_->for_each( [ ]( int i ) {
        std::cout << " " << i;
    } );
    std::cout << "\n";

//    bt.erase( maxx );
//    bt.erase( maxx - 1 );

////    bt.erase( 11 );
////    bt.erase( 8 );
////    bt.erase( 5 );
////    bt.erase( 1 );

    std::cout << "Ok!\n";
//    return 0;

    auto nw = bt.root_->node_with( maxx - 1 );

    if( nw.first ) {
        auto n = nw.first;
        while( n ) {
            print(n->values_);
            if( n->parent_ ) {
                std::cout << n->my_position( ) << "\n";
            }
            n = n->parent_;
        }
    }

    for( auto i=maxx; i>=1; i-- ) {
        bt.erase( i );
    }


//    auto nw = bt.root_->node_with( 3 );
//    //auto nw = bt.root_->node_with( random() % 2100 );

//    if( nw.first ) {

//        auto sb = nw.first->siblings( );

//        print( nw.first->values_ );
//        std::cout << nw.second << "\n";
//    }

    std::cout << "Ok!\n";

    return 0;
}
