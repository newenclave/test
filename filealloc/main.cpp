#include <iostream>
#include <fstream>
#include <stdio.h>

#include "etool/details/byte_order.h"
#include "etool/intervals/set.h"

using namespace etool;

//using page_intervals = intervals::set<>

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

    std::size_t tell( )
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

    std::size_t read( void *data, std::size_t len )
    {
        return fread( data, 1, len, file_ );
    }

private:

    FILE *file_ = nullptr;
};


struct data_source {

    template <typename T>
    using byte_order = details::byte_order_big<T>;

    struct allocated {
        std::size_t block_start;
        std::size_t size;
    };

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
        res.f_.open( data, "rb+" );

        std::string buf( 16, '\0' );

        res.f_.read( &buf[0], 16 );

        auto block_size  = static_cast<std::uint8_t>(buf[4]);
        auto first_block = static_cast<std::uint8_t>(buf[5]);

        res.block_size_  = block2size( block_size );
        res.first_block_ = block2size( first_block );

        return res;
    }

    allocated allocate( std::size_t size )
    {
        auto blocks = size2blocks( size );
        allocated res;

        f_.seek( block2pos( blocks ) - 1 );
        f_.write( "\0", 1 );

        res.size        = blocks * block_size_;
        res.block_start = last_block_;

        last_block_ = blocks;

        return res;
    }

    static
    void create( const std::string &data, std::uint8_t block,
                 std::uint8_t header_size )
    {
        std::string head( "edb", 4 );
        head.push_back( static_cast<char>(block) );
        head.push_back( static_cast<char>( header_size ) );

        head.resize( block2size(header_size) );

        file_source fs(data, "wb");
        std::cout << fs.write( head.c_str( ), head.size( ) ) << "\n";
        fs.flush( );
    }

    static
    std::size_t block2size( std::uint8_t block )
    {
        return ((block + 1) * 512);
    }

    std::size_t size2blocks( std::size_t size )
    {
        return (size / block_size_) + ((size % block_size_) ? 1 : 0);
    }

    std::size_t block2pos( std::size_t block )
    {
        return  (block * block_size_) + first_block_;
    }

    std::size_t write( const allocated &all, std::string data )
    {
        f_.seek( block2pos(all.block_start) );
        return f_.write( data.c_str( ), data.size( ) );
    }

    file_source f_;

    std::size_t block_size_  = 0;
    std::size_t first_block_ = 1;
    std::size_t last_block_  = 1;
};

int main( int argc, char *argv[] )
{

    data_source::create( "/tmp/example.bin", 1, 0 );

    auto ds = data_source::open( "/tmp/example.bin" );

    auto al = ds.allocate( 1025 );
    ds.write( al, "Hello there!" );

    return 0;

    std::fstream fout;
    std::ofstream fin;

    //fin.open( "/tmp/example.bin", std::ios::binary | std::ios::in | std::ios::app );
    //fout.open( "/tmp/example.bin", std::ios::binary | std::ios::in | std::ios::out | std::ios::app );
    if( fout.is_open(  ) ) {
        fout.seekp( 24 * 1024 );
        std::cout << fout.tellp(  ) << "\n";
        fout.write( "qwertyuuio", 10 );
    }

    return 0;
}