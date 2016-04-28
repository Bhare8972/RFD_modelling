﻿

// generate the tables for diffusion due to elastic scattering shielded coulomb scattering

#include <cmath>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <ctime>
#include <list>
#include <thread>
#include <mutex>
#include <sstream>

#include "vector.hpp"
//#include "histogram.hpp"

#include "constants.hpp"
#include "GSL_utils.hpp"
#include "binary_IO.hpp"
#include "arrays_IO.hpp"
#include "functor.hpp"
#include "integrate.hpp"
#include "gen_ex.hpp"

using namespace std;

class hist_tool
{
private:
    class bin
    {
    public:
        double num_counts;
        double lower_range;
        double upper_range;
        list<double> values;
        bool splittable;

        bin(double lower, double upper)
        {
            lower_range=lower;
            upper_range=upper;
            num_counts=0;
            splittable=true;
        }

        bin(bin& splice_from, int I)
        {
            I++;//don't worry about it
            lower_range=(splice_from.lower_range+splice_from.upper_range)/2.0;
            upper_range=splice_from.upper_range;
            splice_from.upper_range=lower_range;
            auto iter=splice_from.values.begin();
            for( ; iter!=splice_from.values.end(); ++iter)
            {
                if((*iter)>=lower_range) break;
            }
            values.splice(values.begin(), splice_from.values, iter, splice_from.values.end());
            num_counts=values.size();
            splice_from.num_counts=splice_from.values.size();
            //print(" lower:", splice_from.lower_range, splice_from.upper_range);
            //print(" upper:", lower_range, upper_range);

            double V=(lower_range+upper_range)/2.0;
            if(V!=lower_range and V!=upper_range)
            {
                splittable=false;
            }
            else
            {
                splittable=true;
            }
        }

        bool increment(double value)
        {
            if(value>=lower_range and value<upper_range)
            {
                if(num_counts==0)
                {
                    values.push_back(value);
                }
                else if(value<values.front())
                {
                    values.push_front(value);
                }
                else if(value>=values.back())
                {
                    values.push_back(value);
                }
                else
                {
                    for(auto iter=(++values.begin()); iter!=values.end(); ++iter)
                    {
                        if(value<(*iter))
                        {
                            --iter;
                            values.insert(iter, value);
                            break;
                        }
                    }
                }

                num_counts++;
                return true;
            }
            else
            {
                return false;
            }
        }
    };

    list<bin> hist_bins;

public:

    typedef list<bin>::iterator iterator;

    hist_tool(double start, double stop, size_t num_bins)
    {
        auto bin_ranges=linspace(start, stop, num_bins+1);
        for(size_t bin_i=0; bin_i<num_bins; bin_i++)
        {
            hist_bins.emplace_back(bin_ranges[bin_i], bin_ranges[bin_i+1]);
        }
    }

    iterator begin()
    {
        return hist_bins.begin();
    }

    iterator end()
    {
        return hist_bins.end();
    }

    void increment(double value)
    {
        for(bin &B : hist_bins)
        {
            if(B.increment(value)) break;
        }
    }

    void split(iterator IT)
    {
        iterator IN=IT;
        hist_bins.emplace(++IN, *IT, 0);
    }

    gsl::vector get_bin_ranges()
    {
        gsl::vector out(hist_bins.size()+1);

        size_t i=0;
        for(bin &B : hist_bins)
        {
            out[i]=B.lower_range;
            i++;
        }

        out[hist_bins.size()]=hist_bins.back().upper_range;

        return out;
    }

    gsl::vector get_bin_values()
    {
        gsl::vector out(hist_bins.size());

        size_t i=0;
        for(bin &B : hist_bins)
        {
            out[i]=B.num_counts;
            i++;
        }

        return out;
    }

    size_t num_bins()
    {
        return hist_bins.size();
    }
};

class diff_cross_section : public functor_1D
{
public:
	double momentum_sq;
	double beta;
	double prefactor;
	double p_factor;
	diff_cross_section(double timestep, double energy_kev=0)
	{
		prefactor=timestep*average_air_atomic_number*average_air_atomic_number/(8*3.1415926);
		p_factor=pow(average_air_atomic_number, 2.0/3.0)/(4*183.3*183.3);

		set_energy(energy_kev);
	}

	void set_energy(double energy_kev)
	{
		double energy=energy_kev*1000.0*elementary_charge/electron_rest_energy;
		momentum_sq=(energy+1.0)*(energy+1.0)-1;
		beta=sqrt(momentum_sq/(1+momentum_sq));
	}

	double cross_section(double angle)
	{
		double S=sin(angle/2.0);
		double numerator=1.0-beta*beta*S*S;
		double denom=S*S+p_factor/momentum_sq;
		return numerator*prefactor/(denom*denom*beta*momentum_sq);
	}

	double integrand(double angle)
	{
		return cross_section(angle)*sin(angle);
	}

	double call(double angle)
	{
		return integrand(angle);
	}
};

class workspace
{
public:
	diff_cross_section cross_section;
	double energy_;

	shared_ptr<poly_quad_spline> spline_sampler; //why do we need a mutex for this?
    mutex sampler_mutex;
	double num_interactions;


	list<thread> threads;

	gsl_rng* rand;
    mutex rand_mutex;

	hist_tool distribution;
    mutex dist_mutex;


	workspace(double timestep, double energy, size_t num_bins_, bool rnd_seed=false) : cross_section(timestep), distribution(0, 3.1415926, num_bins_)
	{
        rand=gsl_rng_alloc(gsl_rng_mt19937);
		if(rnd_seed)
		{
		    unsigned int seed_val( time(NULL) );
		    gsl_rng_set(rand, seed_val);
		}
		else
        {
            gsl_rng_set(rand, 0);
        }

		set_energy(energy);
		//distribution=gsl::histogram(num_bins, 0, 3.1415926);
	}

    ~workspace()
	{
		gsl_rng_free(rand);
	}

    double sample_uniform()
	//gives a uniform random sample between 0 and 1
	{
        lock_guard<mutex> lock(rand_mutex);
		return gsl_rng_uniform(rand);
	}

    unsigned int sample_num_interactions()
	//samples poisson distribtion for number of diffusion interactions
	{
        lock_guard<mutex> lock(rand_mutex);
        return gsl_ran_poisson (rand, num_interactions);
	}

	void set_energy(double energy)
	{
        energy_=energy;
		cross_section.set_energy(energy);
		cum_adap_simps integrator(&cross_section, 0, 3.1415926, 1E4);
		gsl::vector points=integrator.points();
		gsl::vector cum_quads=integrator.cum_quads();
		num_interactions=cum_quads[cum_quads.size()-1]*2*3.1415926;
		cum_quads/=cum_quads[cum_quads.size()-1]; //normalize to values from 0 to 1

		gsl::vector quad_X;
		gsl::vector quad_Y;
        make_fix_spline(cum_quads, points, quad_X, quad_Y);// hope this function will fix the function if the points are singular!
		spline_sampler=make_shared<poly_quad_spline>(quad_X, quad_Y);
		spline_sampler->set_lower_fill(quad_X[0]);
		spline_sampler->set_upper_fill(quad_X[quad_X.size()-1]);


        //shared_ptr<doubles_output> cum_quad_table=make_shared<doubles_output>(cum_quads);
        //shared_ptr<doubles_output> point_table=make_shared<doubles_output>(points);
        //shared_ptr<doubles_output> value_table=make_shared<doubles_output>(integrator.values());
        //shared_ptr<doubles_output> quad_X_table=make_shared<doubles_output>(quad_X);
        //shared_ptr<doubles_output> quad_Y_table=make_shared<doubles_output>(quad_Y);

        //arrays_output array_out;
        //array_out.add_array(cum_quad_table);
        //array_out.add_array(point_table);
        //array_out.add_array(value_table);
        //array_out.add_array(quad_X_table);
        //array_out.add_array(quad_Y_table);

        //binary_output fout("./quadrature_tst");
        //array_out.write_out( &fout);
    }

	double sample_timestep()
	{
	    //get our random samples
        unsigned int num_samples=sample_num_interactions();

        gsl::vector T({0,0,1});

        for(size_t i=0; i<num_samples; i++)
        {
            double inclination_scattering;
            double azimuth_scattering;
            {
                lock_guard<mutex> lock(sampler_mutex);
                inclination_scattering=spline_sampler->call( sample_uniform() ); //transform the sample
                azimuth_scattering=sample_uniform()*2*3.1415926;
            }

            //calculate the three vector magnitudes
            double A=cos(inclination_scattering); //basis vector is original vector
            double B=sin(inclination_scattering)*cos(azimuth_scattering); //basis vector will be vector Bv below
            double C=-sin(inclination_scattering)*sin(azimuth_scattering); //basis vector will be vector Cv below

            //find vector Bv, perpinduclar to momentum
            gsl::vector init({1,0,0});
            gsl::vector Bv=cross(init, T);
            if(Bv.sum_of_squares()<0.1) //init and momentum are close to parellel. Which would cause errors below
            {
                init=gsl::vector({0,1,0}); //so we try a different init. momentum cannot be parrellel to both inits
                Bv=cross(init, T);
            }

            //normalize Bv
            Bv/=sqrt(Bv.sum_of_squares());

            //now we find Cv
            gsl::vector Cv=cross(Bv, T); //Bv and momentum are garenteed to be perpindicular.

            //give Bv correct magnitude
            //Bv*=sqrt(momentum_squared); //correct magnitude is 1

            //find new vector
            T=A*T + B*Bv + C*Cv;
        }

        return acos(T[2]);
	}

	void multi_samples(size_t N)
	{
	    for(size_t i=0; i<N; i++)
	    {
            double sample=sample_timestep();

            {
                lock_guard<mutex> lock(dist_mutex);
                distribution.increment(sample);
                //print("A:", i+lowest_index, final_distribution.size());
                //final_distribution[i+lowest_index]=sample;
                //print("B");
            }
	    }
	}

	void start_thread(size_t samples_perThread_perRun, size_t N_threads, double percent_error)
	{
        if(threads.size() !=0 ) throw gen_exception("must join before starting more threads");

        //sample for the required number of times
        for(size_t i=0; i<N_threads; i++)
        {
            threads.push_back( thread(&workspace::multi_samples, this, samples_perThread_perRun) );
            //multi_samples(samples_per_thread, i*samples_per_thread);
        }

        for(auto& T : threads)
        {
            T.join();
        }
        threads.clear();


        bool keep_running=true;
        size_t n_runs=1;
        while(keep_running)
        {
            gsl::vector old_distribution=distribution.get_bin_values();

            //sample for the required number of times
            for(size_t i=0; i<N_threads; i++)
            {
                threads.push_back( thread(&workspace::multi_samples, this, samples_perThread_perRun) );
                //multi_samples(samples_per_thread, i*samples_per_thread);
            }

            for(auto& T : threads)
            {
                T.join();
            }
            threads.clear();

            //estimate error
            // note that this could use some better mathematical treatment, but it seems to work well

            double error_factor=0;

            gsl::vector new_distribution=distribution.get_bin_values();
            for(size_t i=0; i<new_distribution.size(); i++)
            {
                double N_pnts=new_distribution[i];
                if(N_pnts==0)
                {
                    continue;
                }

                double DH=N_pnts-old_distribution[i];
                double next_error_factor=DH/N_pnts;

                if(next_error_factor>error_factor)
                {
                    error_factor=next_error_factor;
                }
            }

            print(energy_, ":", (n_runs+1)*samples_perThread_perRun*N_threads, "  error:", error_factor, "desired error:", percent_error);

            if(error_factor<percent_error)
            {
                keep_running=false;
            }

            for(auto iter=distribution.begin(); (++iter)!=distribution.end(); ++iter)
            {
                double next_H=iter->num_counts;
                --iter;
                double H=iter->num_counts;

                if(iter->splittable and abs(next_H-H)>3*(sqrt(next_H)+sqrt(H)))
                {
                    keep_running=true;
                    print("SPLIT!:", iter->lower_range, iter->upper_range);
                    distribution.split(iter);
                    //auto ranges=distribution.get_bin_ranges();
                    //for(auto v : ranges)
                    //{
                        //cout<<" "<<v;
                    //}
                    //cout<<endl;
                    break; //WHY IS THIS HERE?
                }
            }

            n_runs++;
        }

	}
//
//	void join_thread()
//    {
//        for(auto& T : threads)
//        {
//            T.join();
//        }
//        threads.clear();
//    }

};

void do_timestep(double time_step)
{
	//double time_step=0.0001;
	double min_energy=0.05; //keV
	double max_energy=100000; //kev
	int num_energies=100;
	size_t threads_per_energy=16; //num threads per energy
	size_t num_samples_per_energy_per_thread_per_run=100;
	size_t num_bins=20; //controlls precision of final distribution
	double error_percent=0.15; //controlls error of bins in y-direction
	bool rnd_seed=false;
	gsl::vector energy_vector=logspace(log10(min_energy), log10(max_energy), num_energies);




    //start procesing for each energy
    std::list<workspace> samplers;
    for(double energy : energy_vector)
    {
        samplers.emplace_back(time_step, energy, num_bins, rnd_seed);
        samplers.back().start_thread(num_samples_per_energy_per_thread_per_run, threads_per_energy, error_percent);
        //samplers.back().join_thread();
    }

    //join threads
//    for(workspace& W : samplers)
//    {
//        W.join_thread();
//    }
    print("writing to file");

    //write to file
    arrays_output tables_out;
    shared_ptr<doubles_output> energies_table=make_shared<doubles_output>(energy_vector);
    tables_out.add_array(energies_table);

    for(workspace& W : samplers)
    {
        shared_ptr<arrays_output> distribution_table=make_shared<arrays_output>();

        shared_ptr<doubles_output> distribution_ranges_table=make_shared<doubles_output>(W.distribution.get_bin_ranges());
        shared_ptr<doubles_output> distribution_values_table=make_shared<doubles_output>(W.distribution.get_bin_values());

        distribution_table->add_array(distribution_ranges_table);
        distribution_table->add_array(distribution_values_table);

        tables_out.add_array(distribution_table);
    }

    stringstream fname;
    fname<<"../tables/diffusion/"<<time_step;
	binary_output fout(fname.str());
	tables_out.write_out( &fout);

}

int main()
{
    do_timestep(0.00005);
    do_timestep(0.0001);
    do_timestep(0.0005);
    do_timestep(0.001);
    do_timestep(0.005);
    do_timestep(0.01);
    do_timestep(0.05);
    do_timestep(0.1);
    do_timestep(0.5);
}
