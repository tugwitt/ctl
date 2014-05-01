/*******************************************************************************
* -Academic Honesty-
* Plagarism: The unauthorized use or close imitation of the language and 
* thoughts of another author and the representation of them as one's own 
* original work, as by not crediting the author. 
* (Encyclopedia Britannica, 2008.)
*
* You are free to use the code according to the license below, but, please
* do not commit acts of academic dishonesty. We strongly encourage and request 
* that for any [academic] use of this source code one should cite one the 
* following works:
* 
* \cite{hatcher, z-ct-10}
* 
* See ct.bib for the corresponding bibtex entries. 
* !!! DO NOT CITE THE USER MANUAL !!!
*******************************************************************************
* Copyright (C) Ryan H. Lewis 2011 <me@ryanlewis.net>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program in a file entitled COPYING; if not, write to the 
* Free Software Foundation, Inc., 
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*******************************************************************************
*******************************************************************************
* NOTES
*
*
*******************************************************************************/
#ifndef _CTL_BUILD_COVER_COMPLEX_H_
#define _CTL_BUILD_COVER_COMPLEX_H_
//STL
#include <numeric>

//TBB
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_scan.h>

//CTL
#include <ctl/chain_complex/complex_boundary.h>
#include <ctl/utility/timer.h>


namespace ctl{
namespace parallel{ 
template<typename Timer>
struct Blowup_stats {
	Timer timer;
	double blowup_time;
	double blowup_ratio;
	double complex_size;
	double blowup_size;
};

// For testing
// Id comparison and cover_cell ordering
template< typename Cell_iterator>
struct Product_first_less  {
  typedef Cell_iterator first_argument_type;
  typedef Cell_iterator second_argument_type;
  typedef         bool result_type;
  bool operator()( const Cell_iterator& c1,
		   const Cell_iterator& c2) const {

    const std::size_t nerve_id_1 = c1->first.first->second.id();
    const std::size_t nerve_id_2 = c2->first.first->second.id();
    //assumes the nerve has been built closed. 
    return (nerve_id_1 < nerve_id_2) || (!(nerve_id_1 > nerve_id_2) && 
	   (c1->first.second_cell() < c2->first.second_cell()));
  }
}; // class Product_first_less

template< typename Cell_iterator>
struct Product_second_less  {
  typedef Cell_iterator first_argument_type;
  typedef Cell_iterator second_argument_type;
  typedef         bool result_type;
  private:
  typedef typename Cell_iterator::value_type::first_type Product_cell;
  typedef typename Product_cell::Cell_iterator1 First_cell_iterator;
  typedef typename Product_cell::Cell_iterator2 Second_cell_iterator;
  public:
  bool operator()( const Cell_iterator& c1,
		   const Cell_iterator& c2) const {

    const std::size_t nerve_id_1 = c1->first.first->second.id();
    const std::size_t nerve_id_2 = c2->first.first->second.id();
    //assumes the nerve has been built closed.
    //also it assumes pointer arithmetic fancyness in or statement to avoid
    //second cell < other_cell comparison 
    return (c1->first.second_cell() < c2->first.second_cell()) || 
		((c1->first.second == c2->first.second) &&  
			nerve_id_1 < nerve_id_2);
  }
}; // class Product_second_less

template< typename Iterator >
struct Filtration_property_map{
    typedef Iterator argument_type;
    typedef typename Iterator::reference result_type; 
    result_type operator()( argument_type & a) const { return *a; }
}; 

template< typename Iterator_property_map,
	  typename Blowup_filtration,
	  typename Nerve_filtration,
	  typename Size_vector,
	  typename Nerve_cell>
class Build_blowup_body{
	private:
	typedef Build_blowup_body< Iterator_property_map, 
				   Blowup_filtration,
				   Nerve_filtration,
				   Size_vector, 
				   Nerve_cell> Self;
	typedef typename Iterator_property_map::argument_type 
						    Complex_filtration_iterator;
	typedef typename Complex_filtration_iterator::value_type Complex_iterator;
	typedef typename Complex_iterator::value_type::second_type Complex_data;
	typedef typename Nerve_cell::value_type::first_type Nerve_simplex;
	typedef typename Nerve_simplex::const_iterator Nerve_cell_iterator;
	typedef typename Complex_iterator::value_type::first_type Cell;
	typedef typename Cell::vertex_type vertex_type;
	typedef typename Blowup_filtration::Complex Blowup;
	typedef typename Nerve_filtration::Complex Nerve;
	typedef typename Blowup::Cell Product;
	typedef typename Blowup::Data Data;
	typedef typename Blowup::Boundary::Boundary1 Nerve_boundary;
	typedef typename Nerve_boundary::const_iterator Boundary_iterator;

	public:
	Build_blowup_body( Iterator_property_map & _get,
			   Blowup_filtration & _filtration,
			   const Nerve_filtration & _nerve_filtration,
			   Size_vector & _sizes, 
			   const Nerve_cell & _impure_cell):
			get( _get),
			filtration ( _filtration),
			nerve_filtration( _nerve_filtration),
			sizes( _sizes),
			impure_cell( _impure_cell),
			current_sizes( _sizes.size(), 0),
			blowup(_filtration.complex()),
			nerve(_nerve_filtration.complex()),
			impure_index( _impure_cell->second.id()),
			boundary( _filtration.complex().boundary().boundary1()){}

	Build_blowup_body( const Self & _self, tbb::split):
			get( _self.get),
			filtration( _self.filtration),
			nerve_filtration( _self.nerve_filtration),
			sizes( _self.sizes), 
			impure_cell( _self.impure_cell),
			current_sizes( _self.current_sizes.size(), 0),
			blowup(_self.blowup), 
			nerve(_self.nerve),
			impure_index( _self.impure_index),
			boundary( _self.boundary){}

	//keep track of partial prefix sum, when we do not
	//need to store the final result.
	template< typename Range>
	void operator()( const Range & r, tbb::pre_scan_tag pre ) {
		typedef typename Range::const_iterator Range_iterator;
		for( Range_iterator i = r.begin(); i != r.end(); ++i){
			const Nerve_cell & sigma = get( i)->second.data();
                        const std::size_t index = sigma->second.id();
			if( index > impure_index){
				current_sizes[ index - impure_index]++;
				current_sizes[ impure_index]++;
			}	
			current_sizes[ index]++;
		}
	}

	private:
	template< typename Cell, typename size_type>
	inline void insert_product_cell( const Nerve_cell & sigma, 
				  	 const Cell & tau,
				  	 const size_type nerve_index){
		const size_t id = sizes[ nerve_index] + 
				  current_sizes[ nerve_index]++;
		Product p( sigma, tau);
		Data d(id+1);
		auto result = blowup.insert_open_cell( p,d);
		filtration[ id] = result.first;
	}




	public:
	//TODO: for correctness this needs to have a loop in it, 
	//and we should specialize everything for case nerve_dim = 2
	//On the final pass over, instead of storing each id in an
	//offset, we simply _build_ the blowup complex
	template< typename Range>
	void operator()( const Range & r, tbb::final_scan_tag pre ) {
		typedef typename Range::const_iterator Iterator;
		std::stringstream ss;
		//assign id's for each cell based on its offsets
		for(Iterator i = r.begin(); i != r.end(); ++i){
			const Complex_iterator & tau = get( i);
			const Nerve_cell & sigma = tau->second.data();
                        const std::size_t index = sigma->second.id();
                        if (index > impure_index){
			   Boundary_iterator & rho = ++boundary.begin( sigma);
			   insert_product_cell( rho->cell(), tau, 
					        index-impure_index);
                           insert_product_cell( impure_cell, tau, impure_index);
                        }
                        insert_product_cell( sigma, tau, index);
                }
	}

	//elementwise a + b (this == b), hence it is reverse_join.
	//this agglomerates the partial sums of sizes of each piece of the 
	//blowup
	void reverse_join( Self & a){
		std::transform( current_sizes.begin(), current_sizes.end(), 
				a.current_sizes.begin(), current_sizes.begin(),
				std::plus< typename Size_vector::value_type>());
	}

	//assign your sum to mine.
        void assign( Self & b){ current_sizes = b.current_sizes;}
	
	private:
	Iterator_property_map & get;
	Blowup_filtration & filtration;
	const Nerve_filtration & nerve_filtration;
	const Size_vector & sizes;
	const Nerve_cell & impure_cell;
	Size_vector current_sizes;
	Blowup & blowup;
	const Nerve & nerve;
	const std::size_t impure_index;
	const Nerve_boundary & boundary;
};

template< typename Iterator, 
          typename Iterator_property_map,
	  typename Nerve_filtration,
          typename Blowup_filtration,
	  typename Stats> 
void build_blowup_complex( Iterator  complex_begin,
			   Iterator  complex_end,
			   Nerve_filtration& nerve_filtration,
                           Blowup_filtration & blowup_filtration,
			   Iterator_property_map & get,
			   Stats & stats){
	//typedef typename Iterator::value_type Complex_iterator;
	typedef typename std::vector< std::size_t> Size_vector;
	typedef typename Nerve_filtration::iterator Nerve_filtration_iterator;
	//typedef typename Complex_iterator::value_type::second_type Complex_data;
	typedef typename Nerve_filtration_iterator::value_type Nerve_cell;
	typedef Build_blowup_body< Iterator_property_map,
				   Blowup_filtration,
				   Nerve_filtration, 
				   Size_vector, 
				   Nerve_cell> Body;
	typedef typename tbb::blocked_range< Iterator> Range;
	//typedef typename Blowup_filtration::Complex Blowup;	
	stats.timer.start();
	Size_vector sizes( nerve_filtration.size()+1, 0);
	//we build a histogram of the blowup filtration buckets
	//from the data stored in the nerve in O(p)
	Nerve_cell impure_cell = *(nerve_filtration.begin());
	for( Nerve_filtration_iterator i = nerve_filtration.begin()+1;
	                               i !=  nerve_filtration.end();
	                               ++i){
	      const std::size_t pos =  (*i)->second.id();
	      sizes[ pos] = sizes[ pos-1] + (*(i-1))->second.count();
	      if ((*i)->first.dimension()==0 && (*(i+1))->first.dimension()==1){
			impure_cell = *i;
	      }
	}
	//we divide up the input complex in its filtration order
	//std::size_t load = std::distance( complex_begin, complex_end);
	Range range( complex_begin, complex_end, 10000);
	//and we compute the  blowup and its filtration in 2m/p + p time.
	Body body( get, blowup_filtration, 
		   nerve_filtration, sizes, 
			        impure_cell);
	tbb::parallel_scan( range, body);
	//blowup_filtration.complex().set_max_id( blowup_filtration.size());
	stats.timer.stop();
	stats.blowup_time = stats.timer.elapsed();
}

template< typename Iterator, 
          typename Blowup_filtration,
	  typename Nerve_filtration,
	  typename Iterator_property_map> 
void build_blowup_complex( Iterator  complex_begin,
			   Iterator  complex_end,
			   Nerve_filtration & nerve_filtration,
                           Blowup_filtration & blowup_filtration,
			   Iterator_property_map & get){
	typedef Blowup_stats< ctl::Timer> Stats;
	Stats not_going_to_be_used;
	build_blowup_complex( complex_begin, 
			      complex_end,
			      nerve_filtration, 
			      blowup_filtration, get, 
			      not_going_to_be_used);
}
} //end namepace parallel
} //end namespace ctl
#endif