#include <vector>
#include <array>
#include <QTextBrowser>
#include "include\mathdt.h"

#ifndef MATRICE_H
#define MATRICE_H

#define _x 0
#define _y 1
#define _z 2

#include <iostream>

class matrice
{
public:
    matrice(unsigned char row, unsigned char col) : _col(col) , _row(row) {
        this->_isSquare = (row==col); 
        //this->_item.resize(_col * _row, 0);
        try{
            this->_item.reserve(_col * _row);
            for(int i = 0; i < _col * _row; i++)    
            {
                this->_item.push_back((i+1) *  std::rand() / RAND_MAX); // Fill with random values between 0 and 1
            }
        }
        catch(const std::bad_alloc& e)
        {
            std::cerr << "Allocation failed: " << e.what() << std::endl;
        }
    };
    ~matrice();

    void printMatrice(QTextEdit* Qtxt);
    double determinant(matrice mat);
    std::vector<double> cofactorMatrix(matrice mat);
    std::vector<double> transpose(matrice mat);
    std::vector<double> adjugateMatrix(matrice mat);
    std::vector<double> inverseMatrix(matrice mat);

//********************************** Getters **********************************
//==============================================================================
    double getItem(unsigned char row, unsigned char col) const {
        if (row >= _row || col >= _col) {
            throw std::out_of_range("Row or column index out of range.");
        }
        return _item[row * _col + col];
    }
    bool isSquare() const {return _isSquare;}
    unsigned char getRow() const {return _row;}
    unsigned char getCol() const {return _col;}
    std::vector<double> getItemVector() const { return _item; }
    std::vector<double>& getItemVectorRef() { return _item; }  // Return a reference to the vector
    std::vector<double> getRowVector(unsigned char row) const {
        if (row >= _row) {
            throw std::out_of_range("Row index out of range.");
        }
        std::vector<double> rowVector(_col);
        for (unsigned char col = 0; col < _col; ++col) {
            rowVector[col] = _item[row * _col + col];
        }
        return rowVector;
    }
    std::vector<double> getColVector(unsigned char col) const {
        if (col >= _col) {
            throw std::out_of_range("Column index out of range.");
        }
        std::vector<double> colVector(_row);
        for (unsigned char row = 0; row < _row; ++row) {
            colVector[row] = _item[row * _col + col];
        }
        return colVector;
    }
    matrice getSubMatrix(unsigned char excludeRow, unsigned char excludeCol) const {
        if (excludeRow >= _row || excludeCol >= _col) {
            throw std::out_of_range("Row or column index out of range.");
        }
        matrice subMatrix(_row - 1, _col - 1);
        unsigned char subRow = 0;
        for (unsigned char row = 0; row < _row; ++row) {
            if (row == excludeRow) continue;
            unsigned char subCol = 0;
            for (unsigned char col = 0; col < _col; ++col) {
                if (col == excludeCol) continue;
                subMatrix.setItem(subRow, subCol, _item[row * _col + col]);
                ++subCol;
            }
            ++subRow;
        }
        return subMatrix;
    }
    matrice getTranspose() {
        matrice transposed(_col, _row);
        for (unsigned char row = 0; row < _row; ++row) {
            for (unsigned char col = 0; col < _col; ++col) {
                transposed.setItem(col, row, _item[row * _col + col]);
            }
        }
        return transposed;
    }
    matrice getAdjugate() {
        matrice adjugate(_row, _col);
        std::vector<double> cofactor = cofactorMatrix(*this);
        for (unsigned char row = 0; row < _row; ++row) {
            for (unsigned char col = 0; col < _col; ++col) {
                adjugate.setItem(col, row, cofactor[row * _col + col]); // Transpose
            }
        }
        return adjugate;
    }
    matrice getInverse() {
        double Determinant = determinant(*this);
        if (Determinant == 0) {
            throw std::logic_error("Matrix is singular and cannot be inverted.");
        }
        matrice adjugate = getAdjugate();
        matrice adjugateTransposed(_row, _col);
        matrice inverse(_row, _col);
        inverse = adjugate.getTranspose();
        inverse*= (1.0 / Determinant);
        return inverse;
    }

    double getDeterminant() {
        return determinant(*this);
    }

    matrice getcofactorMatrix() {
        std::vector<double> cofactor = cofactorMatrix(*this);
        matrice cofactorMat(_row, _col);
        for (unsigned char row = 0; row < _row; ++row) {
            for (unsigned char col = 0; col < _col; ++col) {
                cofactorMat.setItem(row, col, cofactor[row * _col + col]);
            }
        }
        return cofactorMat;
    }

//********************************** Setters **********************************
//==============================================================================
    double setItem(unsigned char row, unsigned char col, double value) {
        if (row >= _row || col >= _col) {
            throw std::out_of_range("Row or column index out of range.");
        }
        _item[row * _col + col] = value;
        return value;
    }

    double setItem(unsigned char index, double value) {
        if (index >= _item.size()) {
            throw std::out_of_range("Index out of range.");
        }
        _item[index] = value;
        return value;
    }

    double setItemVector(const std::vector<double>& values) {
        if (values.size() != _item.size()) {
            throw std::invalid_argument("Input vector size does not match matrix size.");
        }
        _item = values;
        return 0; // Return 0 to indicate success
    }

//********************************** Operator **********************************
//==============================================================================

    matrice operator+(const matrice& other);
    matrice operator-(const matrice& other);

    template<typename T>
    matrice operator*(const T& scalar);
    matrice operator*(const matrice& other);

    template<typename T>
    matrice operator/(const T& scalar);
    matrice operator/(const matrice& other);
    matrice& operator+=(const matrice& other);
    matrice& operator-=(const matrice& other);

    template<typename T>
    matrice& operator*=(const T& scalar);
    matrice& operator*=(const matrice& other);

    template<typename T>
    matrice& operator/=(const T& scalar);
    matrice& operator/=(const matrice& other);
    matrice operator^(const matrice& other);
//********************************** Operator **********************************
//==============================================================================

private:
    std::vector<double> _item;
    bool _isSquare = false;
    unsigned char _row = 0 , _col = 0;
};



class coord3dMatrice
{
    public:
    void initMat();
    ~coord3dMatrice() = default;
    coord3dMatrice() : matRotX(3, 3), matRotY(3, 3), matRotZ(3, 3), matTranslateX(3, 3), matTranslateY(3, 3), matTranslateZ(3, 3), coord(3, 3) {
        this->initMat();
    }
    coord3dMatrice(matrice it) : matRotX(3, 3), matRotY(3, 3), matRotZ(3, 3), matTranslateX(3, 3), matTranslateY(3, 3), matTranslateZ(3, 3), coord(3, 3) {
        this->initMat();
        try{
            if(it.getRow() != 3 || it.getCol() != 3) {
                throw std::invalid_argument("Input matrix must be 3x3.");
            }
            coord = it;
        }
        catch(const std::bad_alloc& e)
        {   
            std::cerr << "Allocation failed: " << e.what() << std::endl;
        }
    }
    coord3dMatrice(std::vector<double> it) : matRotX(3, 3), matRotY(3, 3), matRotZ(3, 3), matTranslateX(3, 3), matTranslateY(3, 3), matTranslateZ(3, 3), coord(3, 3) {
        this->initMat();
        try{
            if(it.size() != 9) {
                throw std::invalid_argument("Input vector size must be 9 for a 3x3 matrix.");
            }
            coord.setItemVector(it);
        }
        catch(const std::bad_alloc& e)
        {
            std::cerr << "Allocation failed: " << e.what() << std::endl;
        }
    }

    /** Setters **/
    void setItemMat(matrice it);

    /** Getteurs **/
    matrice getMatRotX() { return matRotX; }
    matrice getMatRotY() { return matRotY; }
    matrice getMatRotZ() { return matRotZ; }
    matrice getCoord() { return coord; }
    matrice getMatRotXRotatedRad(double angleX) {_rotateXrad(angleX); return matRotX;}
    matrice getMatRotYRotatedRad(double angleY) {_rotateYrad(angleY); return matRotY;}
    matrice getMatRotZRotatedRad(double angleZ) {_rotateZrad(angleZ); return matRotZ;}
    matrice getMatRotXRotatedDeg(double angleX) {_rotateXdeg(angleX); return matRotX;}
    matrice getMatRotYRotatedDeg(double angleY) {_rotateYdeg(angleY); return matRotY;}
    matrice getMatRotZRotatedDeg(double angleZ) {_rotateZdeg(angleZ); return matRotZ;}
    matrice getMatCoordRotatedXrad(double angleX) {return this->getMatRotXRotatedRad(angleX) * coord;}
    matrice getMatCoordRotatedYrad(double angleY) {return this->getMatRotYRotatedRad(angleY) * coord;}
    matrice getMatCoordRotatedZrad(double angleZ) {return this->getMatRotZRotatedRad(angleZ) * coord;}
    matrice getMatCoordRotatedXdeg(double angleX) {return this->getMatRotXRotatedDeg(angleX) * coord;}
    matrice getMatCoordRotatedYdeg(double angleY) {return this->getMatRotYRotatedDeg(angleY) * coord;}
    matrice getMatCoordRotatedZdeg(double angleZ) {return this->getMatRotZRotatedDeg(angleZ) * coord;}
    matrice rotatedMatXrad(matrice mat , double angleX);
    matrice rotateMatYrad(matrice mat , double angleY);
    matrice rotateMatZrad(matrice mat , double angleZ);
    matrice rotatedMatXdeg(matrice mat , double angleX);
    matrice rotateMatYdeg(matrice mat , double angleY);
    matrice rotateMatZdeg(matrice mat , double angleZ);

    matrice getMatCoordTranslatedX(double X) {matTranslateX.setItemVector(std::vector<double>{X, X, X, 0, 0, 0, 0, 0, 0}); return matTranslateX + this->coord;}
    matrice getMatCoordTranslatedY(double Y) {matTranslateY.setItemVector(std::vector<double>{0, 0, 0, Y, Y, Y, 0, 0, 0}); return matTranslateY + this->coord;}
    matrice getMatCoordTranslatedZ(double Z) {matTranslateZ.setItemVector(std::vector<double>{0, 0, 0, 0, 0, 0, Z, Z, Z}); return matTranslateZ + this->coord;}
    matrice translateMatX(matrice mat , double X);
    matrice translateMatY(matrice mat , double Y);
    matrice translateMatZ(matrice mat , double Z);

    private:
    void _rotateXrad(double X);
    void _rotateYrad(double Y);
    void _rotateZrad(double Z);
    void _rotateXdeg(double X);
    void _rotateYdeg(double Y);
    void _rotateZdeg(double Z);
    void _translateX(double X);
    void _translateY(double Y);
    void _translateZ(double Z);
    matrice matRotX, matRotY, matRotZ, matTranslateX, matTranslateY, matTranslateZ;
    matrice coord;
    protected:

};


class vectorCoord
{
    public:
    vectorCoord(unsigned char szVec);
    vectorCoord(std::vector<double> coord);
    vectorCoord(const vectorCoord &vec) : _coord(vec._coord) {} //Copy construct
    ~vectorCoord() = default;

    /*===================== Operator ========================== */
    vectorCoord operator+(const vectorCoord vec);
    vectorCoord& operator+=(const vectorCoord& vec);
    vectorCoord operator-(const vectorCoord vec);
    vectorCoord& operator-=(const vectorCoord& vec);
    vectorCoord operator*(const double scalar);
    vectorCoord operator*(const vectorCoord vec);
    vectorCoord& operator*=(const double scalar);
    vectorCoord& operator*=(const vectorCoord& vec);
    vectorCoord operator/(const double scalar);
    vectorCoord& operator/=(const double scalar);
    vectorCoord operator^(const double scalar);
    vectorCoord& operator^=(const double scalar);
    vectorCoord operator^(const vectorCoord vec);
    vectorCoord& operator^=(const vectorCoord& vec);
    vectorCoord operator<(const vectorCoord vec);
    vectorCoord& operator<=(const vectorCoord& vec);
    vectorCoord& operator=(const vectorCoord& vec);

    /*===================== Setteurs ========================== */
    void setCoord(std::vector<double> vec) {if(vec.size() < 2 || vec.size() > 3) throw std::invalid_argument("invalid size"); this->_coord = vec;}
    void setXcoord(double x) {this->_coord[_x] = x;}
    void setYcoord(double y) {this->_coord[_y] = y;}
    void setZcoord(double z) {this->_coord[_z] = z;}

    /*===================== Getteurs ========================== */
    std::vector<double> getCoord() const { return _coord; }
    double getNorm() const;
    std::vector<double> getDirection() const;
    std::vector <double> getDirectionAngleRad() const;
    std::vector <double> getDirectionAngleDeg() const;
    double getAngleBetweenVectorsRad(const std::vector<double>& vec) const;
    double getAngleBetweenVectorsDeg(const std::vector<double>& vec) const;
    double getXcoord() const { return _coord[_x]; }
    double getYcoord() const { return _coord[_y]; }
    double getZcoord() const { return _coord[_z]; }

    /*===================== Functions ========================== */
    void printVector(QTextEdit* Qtxt) const;

    private:
    std::vector<double> _coord;
    protected:

};

#endif /* MATRICE_H */
