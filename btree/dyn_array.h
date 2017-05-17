#ifndef DYN_ARRAY_H
#define DYN_ARRAY_H

#include <cstdint>
#include <algorithm>

namespace etool {

    template <typename T, std::size_t Max>
    struct dyn_array {

        using value_type      = T;

        using iterator        = value_type *;
        using const_iterator  = value_type const *;

        using reference       = value_type &;
        using const_reference = value_type const &;

        static const size_t maximum = Max;

        dyn_array(  ) = default;

        template <std::size_t S>
        dyn_array( const dyn_array<value_type, S> &other )
        {
            operator = (other);
        }

        template <std::size_t S>
        dyn_array( dyn_array<value_type, S> &&other )
        {
            operator = (other);
        }

        template <std::size_t S>
        dyn_array& operator = ( const dyn_array<value_type, S> &other )
        {
            auto m = std::min(Max, S);
            std::copy( other.begin( ), other.begin( ) + m, begin( ) );
            fill_ = m;
            return *this;
        }

        template <std::size_t S>
        dyn_array& operator = ( dyn_array<value_type, S> &&other )
        {
            auto m = std::min(Max, S);
            fill_ = m;
            while ( m-- ) {
                vals_[m] = std::move(other[m]);
            }
            return *this;
        }

        iterator begin( )
        {
            return &vals_[0];
        }

        iterator end( )
        {
            return &vals_[fill_];
        }

        const_iterator begin( ) const
        {
            return &vals_[0];
        }

        const_iterator end( ) const
        {
            return &vals_[fill_];
        }

        reference operator [ ](size_t pos )
        {
            return vals_[pos];
        }

        const_reference operator [ ](size_t pos ) const
        {
            return vals_[pos];
        }

        void fill( const value_type &val )
        {
            for(std::size_t i=0; i<max_size( ); ++i ) {
                vals_[i] = val;
            }
        }

        std::size_t size ( ) const
        {
            return fill_;
        }

        constexpr
        std::size_t max_size ( ) const
        {
            return maximum;
        }

        bool empty( ) const
        {
            return size( ) == 0;
        }

        bool full( ) const
        {
            return size( ) == max_size( );
        }

        bool clear( )
        {
            return fill_ = 0;
        }

        void reduce( std::size_t count )
        {
            fill_ -= count;
        }

        template<typename... Args>
        iterator emplace( const_iterator pos, Args&&... args )
        {
            iterator last;
            for( last = end( ); last != pos; --last ) {
                *last = std::move( *(last - 1) );
            }
            *last = value_type(std::forward<Args>(args)...);
            fill_ ++;
            return last;
        }

        iterator insert( const_iterator pos, value_type val )
        {
            return emplace(pos, std::move(val));
        }

        iterator erase( iterator pos )
        {
            for(auto tmp = pos; tmp != end( ); ++tmp) {
                *tmp = std::move(*(tmp + 1));
            }
            --fill_;
            return pos;
        }

        template <typename ItrT>
        void assign( ItrT b, ItrT e )
        {
            std::size_t count = 0;
            while( (count != max_size( )) && (b != e) ) {
                vals_[count++] = *(b++);
            }
            if( count != max_size( ) ) {
                fill_ = count;
            }
        }

        template <typename ItrT>
        void assign_move( ItrT b, ItrT e )
        {
            std::size_t count = 0;
            while( (count != max_size( )) && (b != e) ) {
                vals_[count++] = std::move(*(b++));
            }
            if( count != max_size( ) ) {
                fill_ = count;
            }
        }

    private:

        std::size_t fill_ = 0;
        value_type  vals_[maximum];
    };

}

#endif // DYN_ARRAY_H
