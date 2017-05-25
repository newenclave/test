#include <iostream>
#include <fstream>
#include <stdio.h>
#include <set>
#include <map>
#include <list>

#include "etool/details/byte_order.h"
#include "etool/intervals/set.h"
#include "etool/intervals/map.h"

using namespace etool;

using page_intervals = intervals::set<std::uint32_t>;

struct file_source {

    file_source(  ) = default;

    file_source( const std::string &path )
    {
        open( path );
    }

    file_source( const std::string &path, const std::string &mode )
    {
        open( path, mode );
    }

    file_source( const file_source & ) = delete;
    file_source &operator = ( const file_source & ) = delete;

    file_source ( file_source &&other )
        :file_(other.file_)
    {
        other.file_ = nullptr;
    }

    file_source &operator = ( file_source &&other )
    {
        swap( other );
        return *this;
    }

    ~file_source( )
    {
        if(file_) {
            fclose( file_ );
        }
    }

    bool open( const std::string &path )
    {
        file_ = fopen( path.c_str( ), "rb+" );
        return is_open( );
    }

    bool open( const std::string &path, const std::string &mode )
    {
        file_ = fopen( path.c_str( ), mode.c_str( ) );
        return is_open( );
    }

    bool is_open( ) const
    {
        return file_ != nullptr;
    }

    void swap( file_source &other )
    {
        std::swap( file_, other.file_ );
    }

    bool seek( std::size_t pos )
    {
        return fseek( file_, static_cast<long>(pos), SEEK_SET ) == 0;
    }

    std::uint64_t tell( )
    {
        return static_cast<std::size_t>(ftell( file_ ));
    }

    void flush( )
    {
        fflush(file_);
    }

    void close( )
    {
        if( file_ ) {
            fclose( file_ );
            file_ = nullptr;
        }
    }

    std::size_t write( const void *data, std::size_t len )
    {
        return fwrite( data, 1, len, file_ );
    }

    std::size_t write_to( std::uint64_t pos, const void *data, std::size_t len )
    {
        seek( pos );
        return write( data, len );
    }

    std::size_t read( void *data, std::size_t len )
    {
        return fread( data, 1, len, file_ );
    }

private:

    FILE *file_ = nullptr;
};

namespace {

    template <typename T>
    using byte_order = details::byte_order_big<T>;

    struct bytes {
        template <typename T>
        static
        void append( T value, std::string &out )
        {
            auto old_size = out.size( );
            out.resize( old_size + sizeof(value) );
            byte_order<T>::write( value, &out[old_size] );
        }
    };

    using block_size    = std::uint16_t;
    using file_pos      = std::uint64_t;
    using scale_factor  = std::uint8_t;
    using block_id      = std::uint32_t;

    struct free_block {
        block_id count;
        block_id next;

        static
        file_pos size( )
        {
            return sizeof(block_id) * 2;
        }

        std::string serialize( ) const
        {
            std::string res;
            bytes::append( count, res );
            bytes::append( next, res );
            return res;
        }

        void parse( const std::string &from )
        {
            if( from.size( ) >= size( ) ) {
                count = byte_order<block_id>::read( &from[0] );
                next  = byte_order<block_id>::read( &from[sizeof(count)] );
            }
        }

    };

    struct free_block_info {
        block_id    id;
        free_block  block;
        bool        dirty = false;

        free_block_info( block_id i )
            :id(i)
        { }

        free_block_info( )
            :id(0)
        { }

        void make_dirty(  )
        {
            dirty = true;
        }

        void make_clean(  )
        {
            dirty = false;
        }
    };

    inline
    bool operator < ( const free_block_info &left,
                      const free_block_info &right )
    {
        return (left.id < right.id);
    }

    using free_blocks_list = std::list<free_block_info>;
    using free_sizes_map   = std::map<file_pos, free_blocks_list>;
    using free_ivals       = intervals::map<block_id, free_block_info>;

    struct free_block_storage {

        using my_ival = free_ivals::key_type;

        static
        my_ival create( block_id from, block_id count )
        {
            return my_ival::left_closed( from, from + count );
        }

        void remove_from_size( const free_block_info &block )
        {
            auto f = sizes_.find( block.block.count );
            if( f != sizes_.end( ) ) {
                for( auto b = f->second.begin( ); b != f->second.end( ); b++ ) {
                    if( b->id == block.id ) {
                        f->second.erase(b);
                        break;
                    }
                }
                if( f->second.empty( ) ) {
                    sizes_.erase( f );
                }
            }
        }

        void add( const free_block_info &block )
        {
            auto key = create( block.id, block.block.count );

            auto pos = ivals_.insert( std::make_pair( key, block ) );

            if( pos != ivals_.begin( ) ) {
                if( ivals_.left_connected( pos ) ) {

                    pos = std::prev( pos );

                    remove_from_size(pos->second);

                    pos->second.make_dirty( );
                    pos->second.block.count += block.block.count;
                    pos->second.block.next  = std::next(pos)->second.block.next;

                    pos = ivals_.merge_right( pos );

                } else {
                    std::prev(pos)->second.block.next = block.id;
                    std::prev(pos)->second.make_dirty( );
                }
            }

            auto next = std::next( pos );

            if( next != ivals_.end( ) ) {

                if(ivals_.right_connected( pos )) {

                    remove_from_size( next->second );

                    pos->second.block.count += next->second.block.count;
                    pos->second.block.next   = next->second.block.next;
                    pos = ivals_.merge_right( pos );
                    pos->second.make_dirty( );
                } else {
                    pos->second.block.next = std::next(pos)->second.id;
                }
            }

            sizes_[pos->second.block.count].push_front( pos->second );
        }

        block_id allocate( block_id count )
        {
            auto f = sizes_.lower_bound( count );
            if( f != sizes_.end( ) ) {

                block_id res = f->second.front( ).id;
                auto key = create( res, count );
                auto kpos = ivals_.find( key );

                /// equal
                if( count == f->second.front( ).block.count ) {

                    if( kpos != ivals_.begin( ) ) {
                        std::prev( kpos )->second.make_dirty( );
                        std::prev( kpos )->second.block.next =
                                f->second.front( ).block.next;
                    }

                    ivals_.cut( key );
                    f->second.pop_front( );
                    if( f->second.empty( ) ) {
                        sizes_.erase( f );
                    }

                } else { /// count < block.count

                    auto old = f->second.front( );
                    f->second.pop_front( );
                    if( f->second.empty( ) ) {
                        sizes_.erase( f );
                    }

                    old.id += count;
                    old.block.count -= count;
                    old.make_dirty( );

                    if( kpos != ivals_.begin( ) ) {
                        std::prev( kpos )->second.make_dirty( );
                        std::prev( kpos )->second.block.next = old.id;
                    }

                    kpos = ivals_.cut( key );
                    kpos->second = old;

                    sizes_[old.block.count].push_front( old );
                }
                return res;
            }
            return 0;
        }

        free_ivals     ivals_;
        free_sizes_map sizes_;
    };

    struct allocated_block {
        block_id count;
        file_pos usage;

        static
        file_pos size( )
        {
            return sizeof(block_id)
                 + sizeof(file_pos)
                 ;
        }

        std::string serialize( ) const
        {
            std::string res;
            bytes::append( count, res );
            bytes::append( usage, res );
            return res;
        }

        void parse( const std::string &from )
        {
            if( from.size( ) >= size( ) ) {
                count = byte_order<block_id>::read( &from[0] );
                usage = byte_order<file_pos>::read( &from[sizeof(count)] );
            }
        }
    };

    struct block_info {
        block_id id;
        allocated_block block;
    };

}

struct data_source {

    data_source( )
    { }

    data_source( const std::string &data )
        :f_(data)
    { }

    data_source( data_source & ) = delete;
    void operator = ( data_source & ) = delete;

    data_source( data_source &&other )
        :f_(std::move(other.f_))
    { }

    data_source &operator = ( data_source &&other )
    {
        f_.swap( other.f_ );
        return *this;
    }

    static
    data_source open( const std::string &data )
    {
        data_source res;
        res.f_.open( data, "r+b" );

        std::string buf( 16, '\0' );

        auto read_bytes = res.f_.read( &buf[0], 16 );
        if( read_bytes < 16 ) {
            return data_source( );
        }

        auto block_size     = static_cast<std::uint8_t>(buf[4]);
        auto head_block     = static_cast<std::uint8_t>(buf[5]);
        block_id last_id    = byte_order<block_id>::read(&buf[6]);
        block_id first_free = byte_order<block_id>::read(&buf[10]);

        res.block_size_  = block2size( block_size );
        res.header_size_ = block2size( head_block );
        res.last_block_  = last_id;

        res.read_free_block( first_free );

        return res;
    }

    void read_free_block( block_id first )
    {
        std::string header( free_block::size( ), '\0' );

        while( first && first != last_block_ ) {
            auto pos = block2pos( first );
            f_.seek( pos );
            size_t res = f_.read( &header[0], header.size( ) );
            if( res == header.size( ) ) {
                free_block_info next( first );
                next.block.parse( header );
                first = next.block.next;
                free_blocks_.add( next );
            } else {
                return;
            }
        }
    }

    static
    void create( const std::string &data, scale_factor scale,
                 scale_factor header_size )
    {
        std::string head( "edb", 4 );
        head.push_back( static_cast<char>( scale ) );
        head.push_back( static_cast<char>( header_size ) );

        auto old_size = head.size( );
        head.resize( block2size(header_size) );

        /// first and last block
        byte_order<block_id>::write( 1, &head[old_size] ); // last block

        file_source fs(data, "wb");

        fs.write( &head[0], head.size( ) );

        fs.flush( );
    }

    void save_free( )
    {
        for( auto &n: free_blocks_.ivals_ ) {
            if( n.second.dirty ) {
                auto header = n.second.block.serialize( );
                f_.seek( n.second.id );
                f_.write( header.c_str( ), header.size( ) );
            }
        }

        block_id first_id = 0;

        if( !free_blocks_.ivals_.empty( ) ) {
            first_id = free_blocks_.ivals_.begin( )->second.id;
        }

        std::string first;
        bytes::append(first_id, first);
        f_.write_to(10, first.c_str( ), first.size( ));

        first.clear( );
        bytes::append(last_block_, first);
        f_.write_to( 6, first.c_str( ), first.size( ) );

    }

    static constexpr
    block_size block2size( std::uint8_t block )
    {
        return ((static_cast<block_size>(block & 0x7F) + 1) * 512);
    }

    std::uint32_t size2blocks( std::uint64_t size )
    {
        size += allocated_block::size( );
        auto block = (size / block_size_) + ((size % block_size_) ? 1 : 0);
        return static_cast<std::uint32_t>( block & 0xFFFFFFFF );
    }

    file_pos block2pos( block_id block )
    {
        return (static_cast<file_pos>(block - 1) * block_size_) + header_size_;
    }

    file_source f_;

    block_id           header_size_ = 1;
    block_id           last_block_  = 1;
    block_size         block_size_  = 0;
    free_block_storage free_blocks_;

};

int main( int argc, char *argv[] )
{

//    data_source::create( "/tmp/example.bin", 1, 0 );

    auto ds = data_source::open( "/tmp/example.bin" );

    free_block_info first;
    free_block_info second;

    //auto a1 = ds.a

    first.id = 1;
    first.block.count = 2;
    first.block.next  = 3;

    second.id = 3;
    second.block.count = 5;
    second.block.next  = 0;

    ds.free_blocks_.add( first );
    ds.free_blocks_.add( second );

    for( auto &n: ds.free_blocks_.ivals_  ) {
        std::cout << n.first << " -> "
                  << n.second.id << ": "
                  << n.second.block.count << "; "
                  << n.second.block.next << "; "
                  << "\n";
    }

    ds.last_block_ = 7;
    ds.save_free( );

    return 0;
}
