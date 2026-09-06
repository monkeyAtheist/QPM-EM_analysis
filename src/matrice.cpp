#include "matrice.hpp"
#include <iostream>
#include <string>
#include <vector>

matrice::~matrice()
{
}

//********************************** Operator **********************************

matrice matrice::operator+(const matrice& other) {
    if (other._row != _row || other._col != _col) {
        throw std::invalid_argument("Matrices must be of the same size for addition.");
    }   
    matrice result(_row, _col);
    for (size_t i = 0; i < _item.size(); ++i) {
        result._item[i] = _item[i] + other._item[i];
    }
    return result;
}

matrice matrice::operator-(const matrice& other) {
    if (other._row != _row || other._col != _col) {
        throw std::invalid_argument("Matrices must be of the same size for subtraction.");
    }
    matrice result(_row, _col);
    for (size_t i = 0; i < _item.size(); ++i) {
        result._item[i] = _item[i] - other._item[i];
    }
    return result;
}

template<typename T>
matrice matrice::operator*(const T& scalar) {
    matrice result(_row, _col);
    for (size_t i = 0; i < _item.size(); ++i) {
        result._item[i] = _item[i] * scalar;
    }
    return result;
}
matrice matrice::operator*(const matrice& other) {
    if (other._col != _col) {
        throw std::invalid_argument("Incompatible matrix dimensions for multiplication.");
    }
    matrice result(_row, other._col);
    for (size_t i = 0; i < _row; ++i) {
        for (size_t j = 0; j < other._col; ++j) {
            result._item[i * other._col + j] = 0;
            for (size_t k = 0; k < _col; ++k) {
                result._item[i * other._col + j] += _item[i * _col + k] * other._item[k * other._col + j];
            }
        }
    }
    return result;
}

template<typename T>
matrice matrice::operator/(const T& scalar) {
    matrice result(_row, _col);
    for (size_t i = 0; i < _item.size(); ++i) {
        result._item[i] = _item[i] / scalar;
    }
    return result;
}

matrice matrice::operator/(const matrice& other) {
    if(!other._isSquare) {
        std::cerr << "Warning: Division is only defined for square matrices on the right-hand side. Proceeding with element-wise division." << std::endl;
    }
    if (other._col != _col) {
        throw std::invalid_argument("Incompatible matrix dimensions for multiplication.");
    }
    matrice result(_row, _col);
    for (size_t i = 0; i < _row; ++i) {
        for (size_t j = 0; j < _col; ++j) {
            if (other._item[i * other._col + j] == 0) {
                throw std::domain_error("Division by zero encountered in matrix division.");
            }
            result._item[i * _col + j] = _item[i * _col + j] / other._item[i * other._col + j];
        }
    }
    return result;
}

matrice& matrice::operator+=(const matrice& other) {
    if (other._row != _row || other._col != _col) {
        throw std::invalid_argument("Matrices must be of the same size for addition.");
    }
    for (size_t i = 0; i < _item.size(); ++i) {
        _item[i] += other._item[i];
    }
    return *this;
}

matrice& matrice::operator-=(const matrice& other) {
    if (other._row != _row || other._col != _col) {
        throw std::invalid_argument("Matrices must be of the same size for subtraction.");
    }
    for (size_t i = 0; i < _item.size(); ++i) {
        _item[i] -= other._item[i];
    }
    return *this;
}

template<typename T>
matrice& matrice::operator*=(const T& scalar) {
    for (size_t i = 0; i < _item.size(); ++i) {
        _item[i] *= scalar;
    }
    return *this;
}  

matrice& matrice::operator*=(const matrice& other) {
    if (other._col != _col) {
        throw std::invalid_argument("Incompatible matrix dimensions for multiplication.");
    }
    for (size_t i = 0; i < _row; ++i) {
        for (size_t j = 0; j < _col; ++j) {
            _item[i * _col + j] *= other._item[i * other._col + j];
        }
    }
    return *this;
}

template<typename T>
matrice& matrice::operator/=(const T& scalar) {
    for (size_t i = 0; i < _item.size(); ++i) {
        _item[i] /= scalar;
    }
    return *this;
}

matrice& matrice::operator/=(const matrice& other)
{
    if(!other._isSquare) {
        std::cerr << "Warning: Division is only defined for square matrices on the right-hand side. Proceeding with element-wise division." << std::endl;
    }
    if (other._col != _col) {
        throw std::invalid_argument("Incompatible matrix dimensions for multiplication.");
    }
    for (size_t i = 0; i < _row; ++i) {
        for (size_t j = 0; j < _col; ++j) {
            if (other._item[i * other._col + j] == 0) {
                throw std::domain_error("Division by zero encountered in matrix division.");
            }
            _item[i * _col + j] /= other._item[i * other._col + j];
        }
    }
    return *this;
}

matrice matrice::operator^(const matrice& other) {
    if (other._row != _row || other._col != _col) {
        throw std::invalid_argument("Matrices must be of the same size for element-wise exponentiation.");
    }
    matrice result(_row, _col);
    for (size_t i = 0; i < _item.size(); ++i) {
        result._item[i] = std::pow(_item[i], other._item[i]);
    }
    return result;
}

void matrice::printMatrice(QTextEdit *Qtxt)
{
    QString str;
    std::string text = " | ";
    int i = 0;
    try{
        for(i = 1; i <= this->_row * this->_col; i++)
        {
            text += std::to_string(this->_item[i-1]) + " | " ;
            if(!(i % this->_col))
                text += '\n';
        }
    }
    catch(const std::out_of_range& e)
    {
        std::cerr << "Out of range error: " << e.what() << std::endl;
        throw; // Rethrow the exception to be handled by the caller
    }
    str = QString::fromStdString(text);
    std::cout << text << std::endl;
    Qtxt->append(str);
}

/*for 4x4 matrices maximum */
double matrice::determinant(matrice mat) {
    if (!mat._isSquare || mat._row == 0 || mat._col == 0  || mat._row == 1 || mat._col == 1) {
        throw std::logic_error("Determinant is only defined for square matrices.");
    }
    if(mat._row > 4 || mat._col > 4) {
        throw std::logic_error("Determinant calculation is only implemented for matrices up to 4x4.");
    }

    if(mat._row == 2 && mat._col == 2) {
        return mat._item[0] * mat._item[3] - mat._item[1] * mat._item[2];
    } else if(mat._row == 3 && mat._col == 3) {
        return (mat._item[0] * (mat._item[4] * mat._item[8] - mat._item[5] * mat._item[7]) -
               mat._item[1] * (mat._item[3] * mat._item[8] - mat._item[5] * mat._item[6]) +
               mat._item[2] * (mat._item[3] * mat._item[7] - mat._item[4] * mat._item[6]));
    }
    else if(mat._row == 4 && mat._col == 4) {
        return mat._item[0] * (mat._item[5] * (mat._item[10] * mat._item[15] - mat._item[11] * mat._item[14]) -
                          mat._item[6] * (mat._item[9] * mat._item[15] - mat._item[11] * mat._item[13]) +
                          mat._item[7] * (mat._item[9] * mat._item[14] - mat._item[10] * mat._item[13])) -
               mat._item[1] * (mat._item[4] * (mat._item[10] * mat._item[15] - mat._item[11] * mat._item[14]) -
                          mat._item[6] * (mat._item[8] * mat._item[15] - mat._item[11] * mat._item[12]) +
                          mat._item[7] * (mat._item[8] * mat._item[14] - mat._item[10] * mat._item[12])) +
               mat._item[2] * (mat._item[4] * (mat._item[9] * mat._item[15] - mat._item[11] * mat._item[13]) -
                          mat._item[5] * (mat._item[8] * mat._item[15] - mat._item[11] * mat._item[12]) +
                          mat._item[7] * (mat._item[8] * mat._item[13] - mat._item[9]  *mat._item [12])) -
               mat._item [3]*(mat._item [4]*(mat._item [9]*(mat._item [14]-mat._item [10]*(mat._item [8]*(mat._item [13]-mat._item [9]*(mat._item [12]))))));
    }
    return 0.0; // Placeholder
}



    std::vector<double> matrice::cofactorMatrix(matrice mat)
    {
        if(!mat._isSquare || mat._row == 0 || mat._col == 0) {
            throw std::logic_error("Cofactor matrix is only defined for non-empty square matrices.");
        }
        if(mat._row > 4 || mat._col > 4) {
            throw std::logic_error("Cofactor matrix calculation is only implemented for matrices up to 4x4.");
        }

        if(mat._row == 2 && mat._col == 2) {
            return {mat._item[3], -mat._item[1], -mat._item[2], mat._item[0]};
        } else if(mat._row == 3 && mat._col == 3) {
            return {
                mat._item[4] * mat._item[8] - mat._item[5] * mat._item[7],
                -(mat._item[1] * mat._item[8] - mat._item[2] * mat._item[7]),
                mat._item[1] * mat._item[5] - mat._item[2] * mat._item[4],
                -(mat._item[3] * mat._item[8] - mat._item[5] * mat._item[6]),
                mat._item[0] * mat._item[8] - mat._item[2] * mat._item[6],
                -(mat._item[0] * mat._item[5] - mat._item[2] * mat._item[3]),
                mat._item[3] * mat._item[7] - mat._item[4] * mat._item[6],
                -(mat._item[0] * mat._item[7] - mat._item[1] * mat._item[6]),
                mat._item[0] * mat._item[4] - mat._item[1] * mat._item[3]
            };
        }
        else if(mat._row == 4 && mat._col == 4) {
            return{
                mat._item[5] * (mat._item[10] * mat._item[15] - mat._item[11] * mat._item[14]) -
                mat._item[6] * (mat._item[9] * mat._item[15] - mat._item[11] * mat._item[13]) +
                mat._item[7] * (mat._item[9] * mat._item[14] - mat._item[10] * mat._item[13]),

                -(mat._item[4] * (mat._item[10] * mat._item[15] - mat._item[11] * mat._item[14]) -
                  mat._item[6] * (mat._item[8] * mat._item[15] - mat._item[11] * mat._item[12]) +
                  mat._item[7] * (mat._item[8] * mat._item[14] - mat._item[10] * mat._item[12])),

                mat._item[4] * (mat._item[9] * (mat._item [14]-mat._item [10]*(mat._item [8]*(mat._item [13]-mat._item [9]*(mat._item [12])))) -
                -(mat._item [5]*(mat._item [8]*(mat._item [13]-mat._item [9]*(mat._item [12])))) +
                +(mat._item [6]*(mat._item [8]*(mat._item [13]-mat._item [9]*(mat._item [12])))) -
                +(mat._item [7]*(mat._item [8]*(mat._item [13]-mat._item [9]*(mat._item [12])))))
            };
        }
        return std::vector<double>(); // Placeholder
    }

    std::vector<double> matrice::transpose(matrice mat) {
        matrice transposed(mat._col, mat._row);
        for (unsigned char row = 0; row < mat._row; ++row) {
            for (unsigned char col = 0; col < mat._col; ++col) {
                transposed.setItem(col, row, mat.getItem(row, col));
            }
        }
        return transposed._item;
    }

    std::vector<double> matrice::adjugateMatrix(matrice mat)
    {
        if(!mat._isSquare || mat._row == 0 || mat._col == 0) {
            throw std::logic_error("Adjugate matrix is only defined for non-empty square matrices.");
        }
        if(mat._row > 4 || mat._col > 4) {
            throw std::logic_error("Adjugate matrix calculation is only implemented for matrices up to 4x4.");
        }

        std::vector<double> cofactor = cofactorMatrix(mat);
        std::vector<double> adjugate(mat._row * mat._col);

        for(size_t i = 0; i < mat._row; ++i) {
            for(size_t j = 0; j < mat._col; ++j) {
                adjugate[j * mat._row + i] = cofactor[i * mat._col + j]; // Transpose
            }
        }

        return adjugate;
    }

    std::vector<double> matrice::inverseMatrix(matrice mat)
    {
        if(!mat._isSquare || mat._row == 0 || mat._col == 0) {
            throw std::logic_error("Inverse matrix is only defined for non-empty square matrices.");
        }
        if(mat._row > 4 || mat._col > 4) {
            throw std::logic_error("Inverse matrix calculation is only implemented for matrices up to 4x4.");
        }

        double det = mat.getDeterminant();
        if(det == 0) {
            throw std::logic_error("Matrix is singular and cannot be inverted.");
        }

        std::vector<double> adjugate = mat.adjugateMatrix(mat);
        std::vector<double> inverse(mat._row * mat._col);

        for(size_t i = 0; i < mat._row * mat._col; ++i) {
            inverse[i] = adjugate[i] / det;
        }

        return inverse;
    }

//==============================================================================
// CoordMat
//==============================================================================


void coord3dMatrice::initMat() {
    matRotX.setItemVector(std::vector<double>{  1, 0, 0, 
                                                        0, cos(0), -sin(0), 
                                                        0, sin(0), cos(0)});
    matRotY.setItemVector(std::vector<double>{  cos(0), 0, sin(0),
                                                        0, 1, 0,
                                                        -sin(0), 0, cos(0)});
    matRotZ.setItemVector(std::vector<double>{  cos(0), 0, sin(0),
                                                        0, 1, 0,
                                                        -sin(0), 0, cos(0)});
    matTranslateX.setItemVector(std::vector<double>{    1, 0, 0,
                                                                0, 1, 0, 
                                                                0, 0, 1});
    matTranslateY.setItemVector(std::vector<double>{   1, 0, 0,
                                                                0, 1, 0, 
                                                                0, 0, 1});
    matTranslateZ.setItemVector(std::vector<double>{  1, 0, 0,
                                                                0, 1, 0, 
                                                                0, 0, 1});
    coord.setItemVector(std::vector<double>{0, 0, 0, 0, 0, 0, 0, 0, 0});
}


void coord3dMatrice::setItemMat(matrice it) {
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


    void coord3dMatrice::_rotateXrad(double X){
        this->matRotX.setItemVector(std::vector<double>{  1, 0, 0, 
                                                        0, cos(X), -sin(X), 
                                                        0, sin(X), cos(X)});
    }
    void coord3dMatrice::_rotateYrad(double Y){
        this->matRotY.setItemVector(std::vector<double>{  cos(Y), 0, sin(Y),
                                                        0, 1, 0,
                                                        -sin(Y), 0, cos(Y)});
    }
    void coord3dMatrice::_rotateZrad(double Z){
        this->matRotZ.setItemVector(std::vector<double>{  cos(Z), 0, sin(Z),
                                                        0, 1, 0,
                                                        -sin(Z), 0, cos(Z)});
    }
    void coord3dMatrice::_rotateXdeg(double X){
        double deg = X * M_PI / 180.0; 
        this->matRotX.setItemVector(std::vector<double>{  1, 0, 0, 
                                                        0, cos(deg), -sin(deg), 
                                                        0, sin(deg), cos(deg)});
    }
    void coord3dMatrice::_rotateYdeg(double Y){
        double deg = Y * M_PI / 180.0; 
        this->matRotY.setItemVector(std::vector<double>{  cos(deg), 0, sin(deg),
                                                        0, 1, 0,
                                                        -sin(deg), 0, cos(deg)});
    }
    void coord3dMatrice::_rotateZdeg(double Z){
        double deg = Z * M_PI / 180.0; 
        this->matRotZ.setItemVector(std::vector<double>{  cos(deg), 0, sin(deg),
                                                        0, 1, 0,
                                                        -sin(deg), 0, cos(deg)});
    }
    void coord3dMatrice::_translateX(double X){
        this->matTranslateX.setItemVector(std::vector<double>{    X, X, X,
                                                                0, 0, 0, 
                                                                0, 0, 0});
    }
    void coord3dMatrice::_translateY(double Y){
        this->matTranslateY.setItemVector(std::vector<double>{   0, 0, 0,
                                                                Y, Y, Y, 
                                                                0, 0, 0});
    }
    void coord3dMatrice::_translateZ(double Z){
        this->matTranslateZ.setItemVector(std::vector<double>{  0, 0, 0,
                                                                0, 0, 0, 
                                                                Z, Z, Z});
    }



matrice coord3dMatrice::rotatedMatXrad(matrice mat , double angleX) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotXRotatedRad(angleX) * mat;
}

matrice coord3dMatrice::rotateMatYrad(matrice mat , double angleY) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotYRotatedRad(angleY) * mat;
}

matrice coord3dMatrice::rotateMatZrad(matrice mat , double angleZ) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotZRotatedRad(angleZ) * mat;
}

matrice coord3dMatrice::rotatedMatXdeg(matrice mat , double angleX) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotXRotatedDeg(angleX) * mat;
}

matrice coord3dMatrice::rotateMatYdeg(matrice mat , double angleY) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotYRotatedDeg(angleY) * mat;
}

matrice coord3dMatrice::rotateMatZdeg(matrice mat , double angleZ) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return this->getMatRotZRotatedDeg(angleZ) * mat;
}

matrice coord3dMatrice::translateMatX(matrice mat , double X) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    matrice matX = matrice(3, 3);
    matX.setItemVector(std::vector<double>{X, X, X, 0, 0, 0, 0, 0, 0});
    return mat + matX;
}

matrice coord3dMatrice::translateMatY(matrice mat , double Y) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    matrice matY = matrice(3, 3);
    matY.setItemVector(std::vector<double>{0, 0, 0, Y, Y, Y, 0, 0, 0});
    return mat + matY;
}

matrice coord3dMatrice::translateMatZ(matrice mat , double Z) {
    try{
        if(mat.getRow() != 3 || mat.getCol() != 3) {
            throw std::invalid_argument("Input matrix must be 3x3.");
        }
    }
    catch(const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    matrice matZ = matrice(3, 3);
    matZ.setItemVector(std::vector<double>{0, 0, 0, 0, 0, 0, Z, Z, Z});
    return mat + matZ;
}


//==============================================================================
// VectorDimension

vectorCoord::vectorCoord(unsigned char szVec) {
    try{
        if(szVec == 0 || szVec == 1 || szVec > 3) {throw std::invalid_argument("Vector size must be greater than 1 and less than or equal to 3, value passed : " + std::to_string(szVec));}
        this->_coord.reserve(szVec);
    }
    catch(const std::bad_alloc& e)
    {
        std::cerr << "Allocation failed: " << e.what() << std::endl;
    }
}

vectorCoord::vectorCoord(std::vector<double> coord) {
    this->_coord = coord;
    try{
        if(coord.size() == 0 || coord.size() == 1 || coord.size() > 3) {throw std::invalid_argument("Vector size must be greater than 1 and less than or equal to 3, value passed : " + std::to_string(coord.size()));}
        this->_coord.reserve(coord.size());
    }
    catch(const std::bad_alloc& e)
    {
        std::cerr << "Allocation failed: " << e.what() << std::endl;
    }
}

vectorCoord vectorCoord::operator+(const vectorCoord _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }

    std::vector<double> res;
    res.reserve(vec.size());
    for(int i =0; i < vec.size(); i++)
    {
        res.push_back(res[i] + vec[i]);
    }
    vectorCoord result(res);
    return result;
}

vectorCoord& vectorCoord::operator+=(const vectorCoord& _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    for(int i = 0; i < vec.size(); i++)
    {
        this->_coord[i] += vec[i];
    }
    return *this;
}

vectorCoord vectorCoord::operator-(const vectorCoord _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }

    std::vector<double> res;
    res.reserve(vec.size());
    for(int i = 0; i < vec.size(); i++)
    {
        res.push_back(this->_coord[i] - vec[i]);
    }
    vectorCoord result(res);
    return result;
}

vectorCoord& vectorCoord::operator-=(const vectorCoord& _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    for(int i = 0; i < vec.size(); i++)
    {
        this->_coord[i] -= vec[i];
    }
    return *this;
}

vectorCoord vectorCoord::operator*(const double scalar)
{
    std::vector<double> res;
    res.reserve(this->_coord.size());
    for(int i =0; i < this->_coord.size(); i++)
    {
        res.push_back(this->_coord[i] * scalar);
    }
    vectorCoord result(res);
    return result;
}

vectorCoord vectorCoord::operator*(const vectorCoord _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::vector<double> res;
    res.reserve(this->_coord.size());
    for(int i =0; i < this->_coord.size(); i++)
    {
        res.push_back(this->_coord[i] * vec[i]);
    }
    vectorCoord result(res);
    return result;
}


vectorCoord& vectorCoord::operator/=(const double scalar)
{
    for(int i =0; i < this->_coord.size(); i++)
    {
        this->_coord[i] /= scalar;
    }
    return *this;
}  

/** @brief Scalar product.
 *  @param vec The other vector.
 *  @return The result of the scalar product.
 */
vectorCoord& vectorCoord::operator*=(const vectorCoord& _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    for(int i =0; i < vec.size(); i++)
    {
        this->_coord[i] *= vec[i];
    }
    return *this;
}

vectorCoord& vectorCoord::operator*=(const double scalar)
{
    for(int i =0; i < this->_coord.size(); i++)
    {
        this->_coord[i] *= scalar;
    }
    return *this;
}

vectorCoord vectorCoord::operator/(const double scalar)
{
    std::vector<double> res;
    res.reserve(this->_coord.size());
    for(int i =0; i < this->_coord.size(); i++)
    {
        res.push_back(this->_coord[i] / scalar);
    }
    vectorCoord result(res);
    return result;
}

vectorCoord vectorCoord::operator^(double scalar)
{
    std::vector<double> res;
    res.reserve(this->_coord.size());
    for(int i =0; i < this->_coord.size(); i++)
    {
        res.push_back(std::pow(this->_coord[i], scalar));
    }
    vectorCoord result(res);
    return result;
}

vectorCoord& vectorCoord::operator^=(double scalar)
{
    for(int i =0; i < this->_coord.size(); i++)
    {
        this->_coord[i] = std::pow(this->_coord[i], scalar);
    }
    return *this;
}

vectorCoord vectorCoord::operator^(const vectorCoord _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::vector<double> res;
    res.reserve(this->_coord.size());
    for(int i = 0; i < this->_coord.size(); i++)
    {
        res.push_back(std::pow(this->_coord[i], vec[i]));
    }
    vectorCoord result(res);
    return result;
}

vectorCoord& vectorCoord::operator^=(const vectorCoord& _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    for(int i = 0; i < this->_coord.size(); i++)
    {
        this->_coord[i] = std::pow(this->_coord[i], vec[i]);
    }
    return *this;
}

/** @brief Computes the dot vectorial product of this vector with another vector.
 *  @param vec The other vector.
 *  @return The dot product.
 */
vectorCoord vectorCoord::operator<(const vectorCoord _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
        else if(vec.size() != 3)
            throw std::domain_error("cannot compute dot product for vector of size different than 3");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::vector<double> res;
    res.reserve(this->_coord.size());
    res.push_back((this->_coord[_y]*vec[_z]) - (this->_coord[_z]*vec[_y]));
    res.push_back((this->_coord[_z]*vec[_x]) - (this->_coord[_x]*vec[_z]));
    res.push_back((this->_coord[_x]*vec[_y]) - (this->_coord[_y]*vec[_x]));

    vectorCoord result(res);
    return result;
}

vectorCoord& vectorCoord::operator<=(const vectorCoord& _vec)
{
    std::vector<double> vec = _vec.getCoord();
    try{
        if(vec.size() != this->_coord.size())
            throw std::domain_error("cannot compute addition for vector of different size");
        else if(vec.size() != 3)
            throw std::domain_error("cannot compute dot product for vector of size different than 3");
    }
    catch(std::domain_error e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::vector<double> res;
    res.reserve(this->_coord.size());
    res.push_back((this->_coord[_y]*vec[_z]) - (this->_coord[_z]*vec[_y]));
    res.push_back((this->_coord[_z]*vec[_x]) - (this->_coord[_x]*vec[_z]));
    res.push_back((this->_coord[_x]*vec[_y]) - (this->_coord[_y]*vec[_x]));
    for(int i = 0; i < this->_coord.size(); i++)
    {
        this->_coord[i] = res[i];
    }
    return *this;
}

vectorCoord& vectorCoord::operator=(const vectorCoord& vec)
{
    if (this != &vec) {
        this->_coord = vec._coord;
    }
    return *this;
}

double vectorCoord::getNorm() const {
    double sum = 0.0;
    for (double val : _coord) {
        sum += val * val;
    }
    return std::sqrt(sum);
}

std::vector<double> vectorCoord::getDirection() const {
    double norm = getNorm();
    if (norm == 0) {
        throw std::domain_error("Cannot compute direction of a zero vector.");
    }
    std::vector<double> direction;
    direction.reserve(_coord.size());
    for (double val : _coord) {
        direction.push_back(val / norm);
    }
    return direction;
}

std::vector <double> vectorCoord::getDirectionAngleRad() const {
    double norm = getNorm();
    if (norm == 0) {
        throw std::domain_error("Cannot compute direction of a zero vector.");
    }
    std::vector<double> directionAngle;
    directionAngle.reserve(_coord.size());
    for (double val : _coord) {
        directionAngle.push_back(std::atan2(val, norm));
    }
    return directionAngle;
}

std::vector <double> vectorCoord::getDirectionAngleDeg() const {
    double norm = getNorm();
    if (norm == 0) {
        throw std::domain_error("Cannot compute direction of a zero vector.");
    }
    std::vector<double> directionAngle;
    directionAngle.reserve(_coord.size());
    for (double val : _coord) {
        directionAngle.push_back(std::atan2(val, norm) * 180.0 / M_PI);
    }
    return directionAngle;
}

double vectorCoord::getAngleBetweenVectorsRad(const std::vector<double>& vec) const {
    double dotProduct = 0.0;
    for (size_t i = 0; i < _coord.size(); i++) {
        dotProduct += _coord[i] * vec[i];
    }
    double norm1 = getNorm();
    double norm2 = 0.0;
    for (double val : vec) {
        norm2 += val * val;
    }
    norm2 = std::sqrt(norm2);
    if (norm1 == 0 || norm2 == 0) {
        throw std::domain_error("Cannot compute angle between a zero vector and another vector.");
    }
    return std::acos(dotProduct / (norm1 * norm2));
}

double vectorCoord::getAngleBetweenVectorsDeg(const std::vector<double>& vec) const {
    return getAngleBetweenVectorsRad(vec) * 180.0 / M_PI;
}

void vectorCoord::printVector(QTextEdit* Qtxt) const
{
    Qtxt->append(QString::number(_coord[0]) + ", " + QString::number(_coord[1]) + ", " + QString::number(_coord[2]));
}