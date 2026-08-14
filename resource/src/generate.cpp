#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

struct FileInfo {
    std::string arrayName;
    std::string relativePath;
    std::vector<unsigned char> data;
};

std::string escapePath( const std::string &path ) {
    std::string result = path;
    for ( char &c : result ) {
        if ( c == '\\' ) {
            c = '/';
        }
    }
    return result;
}

void generateData( const FileInfo &file, std::ostream &out ) {
    int n;
    if ( file.data.size() % sizeof( unsigned long long ) == 0 ) {
        n = sizeof( unsigned long long );
        out << "static const unsigned long long " << file.arrayName << "[]={";
        unsigned long long value;
        for ( size_t i = 0; i < file.data.size(); i += n ) {
            if ( i > 0 )
                out << ",";
            std::memcpy( &value, &file.data[i], n );
            out << value << "ULL";
        }
        out << "};\n";
    } else if ( file.data.size() % sizeof( unsigned int ) == 0 ) {
        n = sizeof( unsigned int );
        out << "static const unsigned int " << file.arrayName << "[]={";
        unsigned int value;
        for ( size_t i = 0; i < file.data.size(); i += n ) {
            if ( i > 0 )
                out << ",";
            std::memcpy( &value, &file.data[i], n );
            out << value << "U";
        }
        out << "};\n";
    } else if ( file.data.size() % sizeof( unsigned short ) == 0 ) {
        n = sizeof( unsigned short );
        out << "static const unsigned short " << file.arrayName << "[]={";
        unsigned short value;
        for ( size_t i = 0; i < file.data.size(); i += n ) {
            if ( i > 0 )
                out << ",";
            std::memcpy( &value, &file.data[i], n );
            out << value;
        }
        out << "};\n";
    } else {
        n = sizeof( unsigned char );
        out << "static const unsigned char " << file.arrayName << "[]={";
        unsigned char value;
        for ( size_t i = 0; i < file.data.size(); i += n ) {
            if ( i > 0 )
                out << ",";
            std::memcpy( &value, &file.data[i], n );
            out << static_cast<int>( value );
        }
        out << "};\n";
    }
}

bool generateResourceHeader( const std::string &inputDir, const std::string &outputPath ) {
    static int number = 0;
    std::vector<FileInfo> files;

    if ( !fs::exists( inputDir ) || !fs::is_directory( inputDir ) ) {
        std::cerr << "Error: Input directory does not exist: " << inputDir << std::endl;
        return false;
    }

    for ( const auto &entry : fs::recursive_directory_iterator( inputDir ) ) {
        if ( entry.is_regular_file() ) {
            FileInfo info;

            fs::path relPath = fs::relative( entry.path(), inputDir );
            info.relativePath = "/" + escapePath( relPath.string() );
            info.arrayName = "D" + std::to_string( number++ );

            std::ifstream file( entry.path(), std::ios::binary );
            if ( !file ) {
                std::cerr << "Warning: Cannot read file: " << entry.path() << std::endl;
                continue;
            }

            info.data = std::vector<unsigned char>(
                std::istreambuf_iterator<char>( file ),
                std::istreambuf_iterator<char>()

            );

            files.push_back( info );
        }
    }

    std::sort( files.begin(), files.end(), []( const FileInfo &a, const FileInfo &b ) {
        return a.relativePath < b.relativePath;
    } );

    std::ofstream out( outputPath );
    if ( !out ) {
        std::cerr << "Error: Cannot create output file: " << outputPath << std::endl;
        return false;
    }

    fs::path headerName = fs::path( outputPath ).filename();
    std::string guardName = headerName.string();
    for ( char &c : guardName ) {
        if ( c == '.' ) {
            c = '_';
        } else {
            c = std::toupper( c );
        }
    }

    std::time_t now = std::time( nullptr );
    std::tm *local = std::localtime( &now );
    out << "// Generated on: " << std::asctime( local ) << "\n";
    out << "#ifndef " << guardName << "\n";
    out << "#define " << guardName << "\n\n";

    for ( const auto &file : files ) {
        // out << "static const unsigned char " << file.arrayName << "[]={";
        // for ( size_t i = 0; i < file.data.size(); ++i ) {
        //     if ( i > 0 )
        //         out << ",";
        //     out << static_cast<int>( file.data[i] );
        // }
        // out << "};\n\n";
        generateData( file, out );
    }

    out << "// ===========================================\n";
    out << "typedef struct{\n";
    out << "    const char *name;\n";
    out << "    const unsigned char * const data;\n";
    out << "    const int size;\n";
    out << "}RES_Resource;\n";

    out << "#define RES_DataSize " << files.size() << "\n";
    out << "const RES_Resource RES_Data[RES_DataSize]={\n";

    for ( size_t i = 0; i < files.size(); ++i ) {
        out << "    {\""
            << files[i].relativePath << "\","
            << "(const unsigned char*)" << files[i].arrayName << ","
            << files[i].data.size()
            << "}";

        if ( i < files.size() - 1 ) {
            out << ",\n";
        }
    }

    out << "\n};\n\n";
    out << "#endif\n";

    std::cout << "Successfully generated: " << outputPath << std::endl;
    std::cout << "Total files processed: " << files.size() << std::endl;

    return true;
}

int main( int argc, char *argv[] ) {
    if ( generateResourceHeader( INPUT_DIR, OUTPUT_NAME ) )
        return 0;
    return 1;
}