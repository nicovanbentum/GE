#pragma once

#include "pch.h"
#include "util.h"

namespace Raekor {

/////////////////////////////////////////////////////////////////////

class Shader {
public:
    enum class Type { VERTEX, FRAG, GEO, COMPUTE };

    struct Stage {
        Type type;
        const char* filepath;
        std::vector<std::string> defines;

        Stage(Type type, const char* filepath) : type(type), filepath(filepath) {}
    };


    static Shader* construct(Stage* stages, size_t stageCount);
    virtual const void bind() const = 0;
    virtual const void unbind() const = 0;

protected:
    struct loc {
        unsigned int id;

        loc& operator=(const glm::mat4& rhs) {
            glUniformMatrix4fv(id, 1, GL_FALSE, glm::value_ptr(rhs));
            return *this;
        }

        loc& operator=(float rhs) {
            glUniform1f(id, rhs);
            return *this;
        }

        loc& operator=(bool rhs) {
            glUniform1i(id, rhs);
            return *this;
        }

        loc& operator=(const glm::vec4& rhs) {
            glUniform4f(id, rhs.x, rhs.y, rhs.z, rhs.w);
            return *this;
        }

        template<unsigned int Size>
        loc& operator=(const std::array<glm::mat4, Size>& matrices) {
            glUniformMatrix4fv(id, matrices.size(), GL_FALSE, glm::value_ptr(matrices[0]));
            return *this;
        }
    };
};

/////////////////////////////////////////////////////////////////////

class GLShader : public Shader {
public:
    GLShader(Stage* stages, size_t stageCount);
    ~GLShader();

    void reload(Stage* stages, size_t stageCount);

    inline const void bind() const;
    inline const void unbind() const;

    inline unsigned int getID() const { return programID; }

    loc operator[] (const char* data);

private:
    unsigned int programID;
};

} // Namespace Raekor
