#ifndef _VMC_CEPERLEY_MATRIX_HPP
#define _VMC_CEPERLEY_MATRIX_HPP

#include <iostream>
#include <cmath>
#include <utility>

#include <Eigen/Dense>
#include <boost/assert.hpp>

#include "Big.hpp"
#include "lw_vector.hpp"
#include "vmc-typedefs.hpp"

/**
 * O(N^2) method for keeping track of a determinant when only one or a few
 * row(s) or column(s) change in a given step.  Also known as the
 * Sherman-Morrison-Woodbury formula.  This class acts as a finite state
 * machine; that is, its methods should be called in a specific order.  See
 * current_state.
 */
template <typename T>
class CeperleyMatrix
{
private:
    /**
     * Eigen's LU decomposition does not allow access to the sign of the
     * permutation, which is necessary for us to get the sign of the
     * determinant correctly.  Luckily, the sign of the permutation is given as
     * a protected member, so we can access it by using this class to do our LU
     * decomposition.
     */
    class MyFullPivLU : public Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> >
    {
    public:
        MyFullPivLU (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> & matrix)
            : Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> >(matrix)
            {
            }

        int get_det_pq (void) const
            {
                return Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> >::m_det_pq;
            }
    };

    /**
     * An enum for storing the current state of the object
     */
    enum State {
        UNINITIALIZED,
        READY_FOR_UPDATE,
        ROW_UPDATE_IN_PROGRESS,
        COLUMN_UPDATE_IN_PROGRESS,
        COLUMNS_UPDATE_IN_PROGRESS,
        ROWCOL_UPDATE_IN_PROGRESS
    };

    State current_state;

    // We like to recalculate the inverse just to be safe any time the
    // determinant's "base" (in this case, the ratio of its current value
    // compared with when it was last calculated from scratch) drops below the
    // `ceperley_determinant_lower_cutoff`.  In most cases, it is okay if this
    // recalculation is done only if the move is accepted.  However, in some
    // cases the determinant should be recalculated during the "update" step,
    // before the new determinant is reported.  In particular, if a determinant
    // has a negative exponent applied to it, then a very small determinant
    // will result in a large probability weight.  However, a very small
    // determinant often implies that the matrix is singular, in which case
    // there should be no probability at all for the state.  Recalculating the
    // inverse will detect this singularity and get the probability weight
    // correct before deciding to accept the move.  In short, any time a
    // negative exponent is going to be applied to a determinant, then it is
    // best to set this flag to be true.  In most other cases, setting it to
    // false should be fine.
    bool be_extra_careful;

    // old_det, new_invmat, old_data_m, and new_nullity_lower_bound all exist
    // so we can cancel an update if we wish

    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> mat, invmat, new_invmat;
    T detrat;
    Big<T> det, old_det;
    unsigned int pending_index; // refers to a row or column index
    int nullity_lower_bound; // in general, a lower bound on the nullity.  but
                             // it becomes zero only when the nullity is
                             // precisely zero (and the matrix is invertible)
    int new_nullity_lower_bound;
    bool inverse_recalculated_for_current_update;

    // this must be set during an update, unless new_nullity_lower_bound > 0
    // (or unless we are using update_row() or update_column())
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> detrat_inv_m;
    // these must be set during a columns update
    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> old_cols_m, old_rows_m, cols_offset_m, rows_offset_m;
    lw_vector<unsigned int, MAX_MOVE_SIZE> pending_col_indices, pending_row_indices;

    /**
     * As long as the magnitude of the "base" of the determinant remains between
     * these values, the O(N^2) update algorithm will be used.  However, if the
     * "base" falls outside these values we will recalculate the inverse from
     * scratch to fight numerical error.  This also allows us to determine when
     * the matrix is singular.
     */
    static const T ceperley_determinant_lower_cutoff;
    static const T ceperley_determinant_upper_cutoff;

    /**
     * This cutoff is used when be_extra_careful is true.  It is larger in
     * magnitude because it is used as a threshold for checking whether the
     * matrix might have become singular, if/when we need to pay special
     * attention to that.
     */
    static const T ceperley_determinant_safe_lower_cutoff;

public:
    /**
     * Constructor for initializing a CeperleyMatrix from a square Eigen::Matrix
     */
    CeperleyMatrix (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &initial_mat, bool be_extra_careful_=false)
        : current_state(READY_FOR_UPDATE),
          be_extra_careful(be_extra_careful_),
          mat(initial_mat),
          inverse_recalculated_for_current_update(false)
        {
            BOOST_ASSERT(initial_mat.rows() == initial_mat.cols());

            calculate_inverse(false);
        }

    /**
     * It's sometimes useful to have a default constructor, but such objects
     * are useless until they are copied from an existing CeperleyMatrix.
     */
    CeperleyMatrix (void)
        : current_state(UNINITIALIZED)
        {
        }

    /**
     * Call this function to swap two rows in the matrix.  As a result, the
     * determinant will change sign.
     *
     * @param r1 Index of one row to be swapped
     * @param r2 Index of the row to the swapped with
     */
    void swap_rows (unsigned int r1, unsigned int r2)
        {
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(r1 < mat.rows());
            BOOST_ASSERT(r2 < mat.rows());
            BOOST_ASSERT(r1 != r2);

            mat.row(r1).swap(mat.row(r2));
            if (nullity_lower_bound == 0)
                invmat.col(r1).swap(invmat.col(r2));

            det *= -1;
            // NOTE: we don't need to update detrat because it is only relevant
            // when an update is in progress
        }

    /**
     * Call this function to swap two columns in the matrix.  As a result, the
     * determinant will change sign.
     *
     * @param c1 Index of one column to be swapped
     * @param c2 Index of the column to the swapped with
     */
    void swap_columns (unsigned int c1, unsigned int c2)
        {
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(c1 < mat.cols());
            BOOST_ASSERT(c2 < mat.cols());
            BOOST_ASSERT(c1 != c2);

            mat.col(c1).swap(mat.col(c2));
            if (nullity_lower_bound == 0)
                invmat.row(c1).swap(invmat.row(c2));

            det *= -1;
            // NOTE: we don't need to update detrat because it is only relevant
            // when an update is in progress
        }

    /**
     * Update a row in the matrix by replacing it with the given vector.
     *
     * This takes O(N) time, assuming the matrix did not become singular as a
     * result of the most recent update.
     *
     * After this is called, the new determinant is available, but no other
     * operations can be called until finish_row_update() is called.  We don't
     * update the inverse matrix here because it takes O(N^2) time and is
     * irrelevant if the matrix is immediately thrown away (e.g. if the Monte
     * Carlo step is rejected).
     *
     * @param r index of the row to be updated
     *
     * @param row vector containing the values with which the row should be
     * replaced
     *
     * @see finish_row_update()
     * @see update_column()
     */
    void update_row (unsigned int r, const Eigen::Matrix<T, Eigen::Dynamic, 1> &row)
        {
            BOOST_ASSERT(r < mat.rows());
            BOOST_ASSERT(row.rows() == mat.cols());
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(!inverse_recalculated_for_current_update);

            // remember some things in case we decide to cancel the update
            old_cols_m.resize(mat.rows(), 1);
            old_cols_m.col(0) = mat.row(r);
            old_det = det;
            new_nullity_lower_bound = nullity_lower_bound;

            // update matrix
            mat.row(r) = row;
            pending_index = r;

            if (nullity_lower_bound == 0) {
                // The matrix is not singular, so we calculate the new
                // determinant using the Sherman-Morrison-Woodbury formula.
                detrat = mat.row(pending_index) * invmat.col(pending_index);
                det *= detrat;

                if (det.is_nonzero()) {
                    // If the determinant has become sufficiently small, the
                    // matrix might have become singular so we recompute its
                    // inverse from scratch.
                    if (be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_safe_lower_cutoff))
                        calculate_inverse(true);
                } else {
                    // the matrix must have become singular
                    new_nullity_lower_bound = 1;
                }
            } else {
                perform_singular_update(1);
            }

            current_state = ROW_UPDATE_IN_PROGRESS;
        }

    /**
     * Update a column in the matrix by replacing it with the given vector.
     *
     * @see update_row()
     * @see finish_column_update()
     */
    void update_column (unsigned int c, const Eigen::Matrix<T, Eigen::Dynamic, 1> &col)
        {
            BOOST_ASSERT(c < mat.cols());
            BOOST_ASSERT(col.rows() == mat.rows());
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(!inverse_recalculated_for_current_update);

            // remember some things in case we decide to cancel the update
            old_cols_m.resize(mat.rows(), 1);
            old_cols_m.col(0) = mat.col(c);
            old_det = det;
            new_nullity_lower_bound = nullity_lower_bound;

            // update matrix
            mat.col(c) = col;
            pending_index = c;

            if (nullity_lower_bound == 0) {
                // The matrix is not singular, so we calculate the new
                // determinant using the Sherman-Morrison-Woodbury formula.
                detrat = invmat.row(pending_index) * mat.col(pending_index);
                det *= detrat;

                if (det.is_nonzero()) {
                    // If the determinant has become sufficiently small, the
                    // matrix might have become singular so we recompute its
                    // inverse from scratch.
                    if (be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_safe_lower_cutoff))
                        calculate_inverse(true);
                } else {
                    // the matrix must have become singular
                    new_nullity_lower_bound = 1;
                }
            } else {
                perform_singular_update(1);
            }

            current_state = COLUMN_UPDATE_IN_PROGRESS;
        }

    /**
     * Update one or more columns in the matrix.
     *
     * The first element of each pair represents the column in the matrix to be
     * replaced.  The second element of the pair represents which column of
     * srcmat it should be replaced with.  (The point of this scheme is to
     * prevent memory from being needlessly copied.)
     *
     * @see finish_columns_update()
     */
    void update_columns (const lw_vector<std::pair<unsigned int, unsigned int>, MAX_MOVE_SIZE> &cols, const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &srcmat)
        {
            BOOST_ASSERT(cols.size() > 0);
            BOOST_ASSERT(cols.size() <= (unsigned int) mat.cols());
            BOOST_ASSERT(srcmat.rows() == mat.rows());
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(!inverse_recalculated_for_current_update);
            BOOST_ASSERT(nullity_lower_bound >= 0);

            // remember some things in case we decide to cancel the update
            old_cols_m.resize(mat.rows(), cols.size());
            cols_offset_m.resize(mat.rows(), cols.size());
            pending_col_indices.resize(0);
            for (unsigned int i = 0; i < cols.size(); ++i) {
#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
                BOOST_ASSERT(cols[i].second < srcmat.cols());
                BOOST_ASSERT(cols[i].first < mat.cols());
                for (unsigned int j = 0; j < i; ++j)
                    BOOST_ASSERT(cols[i].first != cols[j].first);
#endif
                old_cols_m.col(i) = mat.col(cols[i].first);
                pending_col_indices.push_back(cols[i].first);
                // might as well update the matrix within this loop as well
                //
                // NOTE: the below lines seem redundant (adding and
                // substracting the same vector), but it is essential that we
                // base everything around cols_offset_m for stability.
                cols_offset_m.col(i) = srcmat.col(cols[i].second) - mat.col(cols[i].first);
                mat.col(cols[i].first) += cols_offset_m.col(i);
            }
            old_det = det;
            new_nullity_lower_bound = nullity_lower_bound;

            if (nullity_lower_bound != 0) {
                perform_singular_update(cols.size());
            } else {
                // The matrix is not singular, so we calculate the new
                // determinant using the Sherman-Morrison-Woodbury formula.
                Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> detrat_m(cols.size(), cols.size());
                for (unsigned int i = 0; i < cols.size(); ++i) {
                    for (unsigned int j = 0; j < cols.size(); ++j) {
                        detrat_m(i, j) = invmat.row(cols[i].first) * cols_offset_m.col(j);
                    }
                    detrat_m(i, i) += 1; // add the identity matrix
                }

                // we need the determinant and inverse of detrat_m
                if (cols.size() == 1) {
                    // for a 1x1 matrix, there's no need to do a decomposition
                    detrat = detrat_m(0, 0);
                    if (detrat != T(0)) {
                        detrat_inv_m.resize(1, 1);
                        detrat_inv_m(0, 0) = T(1) / detrat;
                    }
                } else {
                    // lu decomposition
                    Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> > lu_decomposition(detrat_m);
                    if (lu_decomposition.isInvertible()) {
                        detrat = lu_decomposition.determinant();
                        detrat_inv_m = lu_decomposition.inverse();
                    } else {
                        // oddly enough, lu_decomposition.determinant() is not
                        // guaranteed to be zero, so we handle this case explicitly
                        detrat = T(0);
                    }
                }

                // update the value of the determinant
                det *= detrat;

                // handle cases in which the matrix has (possibly) become
                // singular
                if (det.is_zero()) {
                    // mark that the matrix has become singular
                    new_nullity_lower_bound = 1;
                } else {
                    // If the determinant has become sufficiently small, the matrix
                    // might have become singular so we recompute its inverse from
                    // scratch just to be safe.
                    if (be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_safe_lower_cutoff))
                        calculate_inverse(true);
                }
            }

            current_state = COLUMNS_UPDATE_IN_PROGRESS;
        }

    /**
     * Update one or more columns and/or rows in the matrix by replacing them
     * with the given entries in srcmat.
     *
     * All entries in srcmat outside the given rows and columns are irrelevant
     * and as such are ignored.
     *
     * This requires O(N) time if we are updating only rows or only columns.
     * If we are updating both it requires O(N^2) time.
     */
    void update_rows_and_columns (const lw_vector<unsigned int, MAX_MOVE_SIZE> &rows, const lw_vector<unsigned int, MAX_MOVE_SIZE> &cols, const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &srcmat)
        {
            BOOST_ASSERT(cols.size() > 0 || rows.size() > 0);
            BOOST_ASSERT(cols.size() <= (unsigned int) mat.cols());
            BOOST_ASSERT(rows.size() <= (unsigned int) mat.rows());
            BOOST_ASSERT(srcmat.rows() == mat.rows());
            BOOST_ASSERT(srcmat.cols() == mat.cols());
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(!inverse_recalculated_for_current_update);
            BOOST_ASSERT(nullity_lower_bound >= 0);

            // remember and update rows
            old_rows_m.resize(rows.size(), mat.cols());
            rows_offset_m.resizeLike(old_rows_m);
            pending_row_indices = rows;
            for (unsigned int i = 0; i < rows.size(); ++i) {
#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
                BOOST_ASSERT(rows[i] < mat.rows());
                for (unsigned int j = 0; j < i; ++j)
                    BOOST_ASSERT(rows[i] != rows[j]);
#endif
                // remember old row
                old_rows_m.row(i) = mat.row(rows[i]);
                // NOTE: the below lines seem redundant (adding and
                // substracting the same vector), but it is essential that we
                // base everything around rows_offset_m for stability.
                rows_offset_m.row(i) = srcmat.row(rows[i]) - mat.row(rows[i]);
                mat.row(rows[i]) += rows_offset_m.row(i);
            }

            // remember and update columns
            old_cols_m.resize(mat.rows(), cols.size());
            cols_offset_m.resizeLike(old_cols_m);
            pending_col_indices = cols;
            for (unsigned int i = 0; i < cols.size(); ++i) {
#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
                BOOST_ASSERT(cols[i] < mat.cols());
                for (unsigned int j = 0; j < i; ++j)
                    BOOST_ASSERT(cols[i] != cols[j]);
#endif
                // remember old column
                old_cols_m.col(i) = mat.col(cols[i]);
                // NOTE: the below lines seem redundant (adding and
                // substracting the same vector), but it is essential that we
                // base everything around cols_offset_m for stability.
                cols_offset_m.col(i) = srcmat.col(cols[i]) - mat.col(cols[i]);
                mat.col(cols[i]) += cols_offset_m.col(i);
            }

            // remember old determinant, etc
            old_det = det;
            new_nullity_lower_bound = nullity_lower_bound;

            if (nullity_lower_bound != 0) {
                perform_singular_update(cols.size());
            } else {
                // The matrix is not singular, so we calculate the new
                // determinant using the Sherman-Morrison-Woodbury formula.
                const unsigned int nr = rows.size(), nc = cols.size();
                Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> detrat_m(nc + nr, nc + nr);
                for (unsigned int i = 0; i < nc; ++i) {
                    for (unsigned int j = 0; j < nc; ++j)
                        detrat_m(i, j) = invmat.row(cols[i]) * cols_offset_m.col(j);
                    for (unsigned int j = 0; j < nr; ++j)
                        detrat_m(i, j + nc) = invmat(cols[i], rows[j]);
                    detrat_m(i, i) += 1; // add the identity matrix
                }
                for (unsigned int i = 0; i < nr; ++i) {
                    for (unsigned int j = 0; j < nc; ++j)
                        // the following line requires O(N^2) steps
                        detrat_m(i + nc, j) = rows_offset_m.row(i) * invmat * cols_offset_m.col(j);
                    for (unsigned int j = 0; j < nr; ++j)
                        detrat_m(i + nc, j + nc) = rows_offset_m.row(i) * invmat.col(rows[j]);
                    detrat_m(i + nc, i + nc) += 1; // add the identity matrix
                }
                // we need the determinant and inverse of detrat_m
                if (detrat_m.cols() == 1) {
                    // for a 1x1 matrix, there's no need to do a decomposition
                    detrat = detrat_m(0, 0);
                    if (detrat != T(0)) {
                        detrat_inv_m.resize(1, 1);
                        detrat_inv_m(0, 0) = T(1) / detrat;
                    }
                } else {
                    // lu decomposition
                    Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> > lu_decomposition(detrat_m);
                    if (lu_decomposition.isInvertible()) {
                        detrat = lu_decomposition.determinant();
                        detrat_inv_m = lu_decomposition.inverse();
                    } else {
                        // oddly enough, lu_decomposition.determinant() is not
                        // guaranteed to be zero, so we handle this case explicitly
                        detrat = T(0);
                    }
                }

                // update the value of the determinant
                det *= detrat;

                // handle cases in which the matrix has (possibly) become
                // singular
                if (det.is_zero()) {
                    // mark that the matrix has become singular
                    new_nullity_lower_bound = 1;
                } else {
                    // If the determinant has become sufficiently small, the matrix
                    // might have become singular so we recompute its inverse from
                    // scratch just to be safe.
                    if (be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_safe_lower_cutoff))
                        calculate_inverse(true);
                }
            }

            current_state = ROWCOL_UPDATE_IN_PROGRESS;
        }

    /**
     * Finish a row update.  Must be called after update_row().
     *
     * This takes O(N^2) time.
     *
     * @see update_row()
     * @see finish_column_update()
     */
    void finish_row_update (void)
        {
            BOOST_ASSERT(current_state == ROW_UPDATE_IN_PROGRESS);

            if (new_nullity_lower_bound == 0 && !inverse_recalculated_for_current_update) {
                if ((!be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_lower_cutoff))
                    || std::abs(det.get_base()) > std::abs(ceperley_determinant_upper_cutoff)) {
                    calculate_inverse(true);
                } else {
                    // implement equation (12) of Ceperley et al, correctly given
                    // as eqn (4.22) of Kent's thesis
                    // http://www.ornl.gov/~pk7/thesis/thesis.ps.gz
                    Eigen::Matrix<T, Eigen::Dynamic, 1> oldcol(invmat.col(pending_index));
                    invmat -= ((invmat.col(pending_index) / detrat) * (mat.row(pending_index) * invmat)).eval();
                    invmat.col(pending_index) = oldcol / detrat;
                }
            }

            nullity_lower_bound = new_nullity_lower_bound;
            if (inverse_recalculated_for_current_update)
                // in theory, invmat.swap(new_invmat) would only swap the
                // pointers (see Eigen/src/Core/Matrix.h), but the code runs
                // slower if we do that instead of a matrix copy here.  it's
                // not at all clear why.
                invmat = new_invmat;
            inverse_recalculated_for_current_update = false;
            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    /**
     * Finish a column update.  Must be called after update_column().
     *
     * @see update_column()
     * @see finish_row_update()
     */
    void finish_column_update (void)
        {
            BOOST_ASSERT(current_state == COLUMN_UPDATE_IN_PROGRESS);

            if (new_nullity_lower_bound == 0 && !inverse_recalculated_for_current_update) {
                if ((!be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_lower_cutoff))
                    || std::abs(det.get_base()) > std::abs(ceperley_determinant_upper_cutoff)) {
                    calculate_inverse(true);
                } else {
                    // same as above in finish_row_update(): update the inverse
                    // matrix
                    Eigen::Matrix<T, Eigen::Dynamic, 1> oldrow(invmat.row(pending_index));
                    invmat -= ((invmat * mat.col(pending_index)) * (invmat.row(pending_index) / detrat)).eval();
                    invmat.row(pending_index) = oldrow / detrat;
                }
            }

            nullity_lower_bound = new_nullity_lower_bound;
            if (inverse_recalculated_for_current_update)
                invmat = new_invmat;
            inverse_recalculated_for_current_update = false;
            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    /**
     * Finish a [multi-]column update.  Must be called after update_columns().
     *
     * @see update_columns()
     */
    void finish_columns_update (void)
        {
            BOOST_ASSERT(current_state == COLUMNS_UPDATE_IN_PROGRESS);

            if (new_nullity_lower_bound == 0 && !inverse_recalculated_for_current_update) {
                if ((!be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_lower_cutoff))
                    || std::abs(det.get_base()) > std::abs(ceperley_determinant_upper_cutoff)) {
                    calculate_inverse(true);
                } else {
                    // same as above in finish_row_update(): update the inverse
                    // matrix
                    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> invmat_offset(Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>::Zero(invmat.rows(), invmat.cols()));
                    for (unsigned int i = 0; i < pending_col_indices.size(); ++i)
                        invmat_offset -= invmat * ((cols_offset_m * detrat_inv_m.col(i)) * invmat.row(pending_col_indices[i]));
                    invmat += invmat_offset;
                    // fixme: we could make a special case for just updating one
                    // column (as there's no need to create invmat_offset or to
                    // loop)
                }
            }

            nullity_lower_bound = new_nullity_lower_bound;
            if (inverse_recalculated_for_current_update)
                invmat = new_invmat;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    /**
     * Finish a [row[s]]+[column[s]] update.  Must be called after update_rows_and_columns().
     *
     * This takes O(N^2) time.
     *
     * @see update_rows_and_columns()
     */
    void finish_rows_and_columns_update (void)
        {
            BOOST_ASSERT(current_state == ROWCOL_UPDATE_IN_PROGRESS);

            if (new_nullity_lower_bound == 0 && !inverse_recalculated_for_current_update) {
                if ((!be_extra_careful && std::abs(det.get_base()) < std::abs(ceperley_determinant_lower_cutoff))
                    || std::abs(det.get_base()) > std::abs(ceperley_determinant_upper_cutoff)) {
                    calculate_inverse(true);
                } else {
                    // update the inverse matrix using Sherman-Morrison-Woodbury
                    Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> invmat_offset(Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>::Zero(invmat.rows(), invmat.cols()));
                    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> cm(invmat * cols_offset_m), rm(rows_offset_m * invmat);
                    const unsigned int nr = pending_row_indices.size(), nc = pending_col_indices.size();
                    for (unsigned int i = 0; i < nc; ++i) {
                        invmat_offset -= (cm * detrat_inv_m.block(0, i, nc, 1)) * invmat.row(pending_col_indices[i]);
                        for (unsigned int j = 0; j < nr; ++j)
                            invmat_offset -= invmat.col(pending_row_indices[j]) * detrat_inv_m(j + nc, i) * invmat.row(pending_col_indices[i]);
                    }
                    for (unsigned int j = 0; j < nr; ++j)
                        invmat_offset -= invmat.col(pending_row_indices[j]) * (detrat_inv_m.block(j + nc, nc, 1, nr) * rm);
                    invmat_offset -= cm * detrat_inv_m.block(0, nc, nc, nr) * rm;
                    invmat += invmat_offset;
                }
            }

            nullity_lower_bound = new_nullity_lower_bound;
            if (inverse_recalculated_for_current_update)
                invmat = new_invmat;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    void cancel_row_update (void)
        {
            BOOST_ASSERT(current_state == ROW_UPDATE_IN_PROGRESS);

            mat.row(pending_index) = old_cols_m.col(0);
            det = old_det;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    void cancel_column_update (void)
        {
            BOOST_ASSERT(current_state == COLUMN_UPDATE_IN_PROGRESS);

            mat.col(pending_index) = old_cols_m.col(0);
            det = old_det;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    void cancel_columns_update (void)
        {
            BOOST_ASSERT(current_state == COLUMNS_UPDATE_IN_PROGRESS);

            for (unsigned int i = 0; i < pending_col_indices.size(); ++i)
                mat.col(pending_col_indices[i]) = old_cols_m.col(i);
            det = old_det;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    void cancel_rows_and_columns_update (void)
        {
            BOOST_ASSERT(current_state == ROWCOL_UPDATE_IN_PROGRESS);

            // NOTE: we must cancel the rows after the columns since we saved
            // the old columns after the rows had already been updated
            for (unsigned int i = 0; i < pending_col_indices.size(); ++i)
                mat.col(pending_col_indices[i]) = old_cols_m.col(i);
            for (unsigned int i = 0; i < pending_row_indices.size(); ++i)
                mat.row(pending_row_indices[i]) = old_rows_m.row(i);
            det = old_det;
            inverse_recalculated_for_current_update = false;

            current_state = READY_FOR_UPDATE;

#ifdef VMC_CAREFUL
            be_careful();
#endif
        }

    /**
     * Recalculates the matrix inverse and determinant from scratch.
     */
    void refresh_state (void)
        {
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            calculate_inverse(false);
        }

    /**
     * Returns the matrix.
     */
    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> & get_matrix (void) const
        {
            BOOST_ASSERT(current_state != UNINITIALIZED);
            return mat;
        }

    /**
     * Returns the inverse matrix.
     */
    const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> & get_inverse (void) const
        {
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            BOOST_ASSERT(nullity_lower_bound == 0);
            return invmat;
        }

    /**
     * Returns the determinant of the matrix.
     *
     * The determinant is always pre-calculated, so this runs in O(1) time.
     */
    const Big<T> & get_determinant (void) const
        {
            BOOST_ASSERT(current_state != UNINITIALIZED);
            return det;
        }

    /**
     * Returns true if the matrix is currently singular
     */
    bool is_singular (void) const
        {
            BOOST_ASSERT(current_state != UNINITIALIZED);
            return det.is_zero();
        }

    /**
     * Computes and returns the inverse matrix error.
     *
     * This multiplies the matrix by its supposed inverse, and compares it to
     * the identity matrix.
     *
     * @return a nonnegative number which is the sum of the absolute error.
     */
    double compute_inverse_matrix_error (const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> &target_invmat) const
        {
            return (mat * target_invmat - Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>::Identity(mat.rows(), mat.cols())).array().abs().sum();
        }

    /**
     * Calculates and returns the determinant error.
     *
     * @return a ratio of the absolute error over the newly-calculated
     * determinant
     */
    double compute_relative_determinant_error (void) const
        {
            BOOST_ASSERT(current_state == READY_FOR_UPDATE);
            Eigen::FullPivLU<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> > lu_decomposition(mat);
            if (lu_decomposition.isInvertible()) {
                T d = lu_decomposition.determinant();
                return std::abs((d - det.get_value()) / d);
            } else {
                return std::abs(det.get_value());
            }
        }

    /**
     * Number of rows in the matrix
     *
     * (Since the matrix is square, this will always be equal to the number of
     * columns)
     *
     * @see cols()
     */
    unsigned int rows (void) const
        {
            BOOST_ASSERT(current_state != UNINITIALIZED);
            return mat.rows();
        }

    /**
     * Number of columns in the matrix
     *
     * (Since the matrix is square, this will always be equal to the number of
     * rows)
     *
     * @see rows()
     */
    unsigned int cols (void) const
        {
            BOOST_ASSERT(current_state != UNINITIALIZED);
            return rows();
        }

private:
    void calculate_inverse (bool update_in_progress)
        {
#if defined(DEBUG_CEPERLEY_MATRIX) || defined(DEBUG_VMC_ALL)
            std::cerr << "calculating an inverse: " << update_in_progress << std::endl;
#endif

            MyFullPivLU lu_decomposition(mat);

            // fixme (?)
            lu_decomposition.setThreshold(lu_decomposition.threshold() * 10);

            (update_in_progress ? new_nullity_lower_bound : nullity_lower_bound) = mat.cols() - lu_decomposition.rank();
            if (!lu_decomposition.isInvertible()) {
                // oddly enough, lu_decomposition.determinant() is not
                // guaranteed to return zero, so we handle this case
                // explicitly.
                det = Big<T>(); // set to zero
            } else {
                // store the determinant as a Big<T>
                const Eigen::Array<T, Eigen::Dynamic, 1> diagonal(lu_decomposition.matrixLU().diagonal().array());
                T phase(lu_decomposition.get_det_pq());
                for (unsigned int i = 0; i < diagonal.rows(); ++i)
                    phase *= diagonal(i) / std::abs(diagonal(i));
                det = Big<T>(phase, diagonal.abs().log().sum());

                (update_in_progress ? new_invmat : invmat) = lu_decomposition.inverse();

#ifndef DISABLE_CEPERLEY_MATRIX_INVERSE_CHECK
                // if there is significant inverse error it probably means our
                // orbitals are not linearly independent!
                const double inverse_error = compute_inverse_matrix_error(update_in_progress ? new_invmat : invmat);
                if (inverse_error > .0001)
                    std::cerr << "Warning: inverse matrix error of " << inverse_error << std::endl;
#endif
            }

            inverse_recalculated_for_current_update = update_in_progress;
        }

    void perform_singular_update (unsigned int update_rank)
        {
            // The matrix was singular on the last step, so we may need to
            // check to see if it is still singular
#if defined(DEBUG_CEPERLEY_MATRIX) || defined(DEBUG_VMC_ALL)
            std::cerr << "DEBUG INFO: matrix was singular!" << std::endl;
#endif
            BOOST_ASSERT(det.is_zero());
            BOOST_ASSERT(new_nullity_lower_bound == nullity_lower_bound);
            BOOST_ASSERT(new_nullity_lower_bound > 0);
            new_nullity_lower_bound -= update_rank;
            if (new_nullity_lower_bound <= 0)
                calculate_inverse(true);
        }

#ifdef VMC_CAREFUL
    void be_careful (void) const
        {
            if (det.is_nonzero() && compute_inverse_matrix_error(invmat) > 1)
                std::cerr << "Large inverse matrix error of " << compute_inverse_matrix_error(invmat) << std::endl;

            if (!(compute_relative_determinant_error() < .03))
                std::cerr << "large determinant error! " << compute_relative_determinant_error() << std::endl;
        }
#endif
};

#endif
